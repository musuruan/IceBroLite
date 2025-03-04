#ifdef _WIN32
#include "winsock2.h"
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/errno.h>
#include <netdb.h>
#endif
#include <inttypes.h>
#include <stdio.h>
#include <malloc.h>
#include <queue>
#include <vector>
#include <assert.h>

#include "Files.h"
#include "struse/struse.h"
#include "6510.h"
#include "Breakpoints.h"
#include "Traces.h"

#include "ViceInterface.h"
#include "ViceBinInterface.h"
#include "platform.h"
#include "views/Views.h"

#ifdef _DEBUG
#define VICELOG
#endif

#define SEND_IMMEDIATE
										 
#ifdef _WIN32
#define VI_SOCKET SOCKET
#else
#define WINAPI
#define VI_SOCKET int
#define strcpy_s strcpy
#define OutputDebugStringA printf
#define SOCKET_ERROR -1
static void Sleep(int ms) {
	timespec t = { 0, ms * 1000 };
	nanosleep(&t, &t);
}
#endif

class ViceConnection {
	enum { RECEIVE_SIZE = 10*1024*1024 };
	struct ViceMessage {
		int size;	// data follows after len..
	};

	size_t waitCount;
	char ipAddress[32];
	uint32_t ipPort;
	bool connected;
	bool stopped;

#ifndef SEND_IMMEDIATE
	std::queue<ViceMessage*> toSend;
#endif
public:
	ViceConnection(const char* ip, uint32_t port);
	~ViceConnection();

	static IBThreadRet WINAPI ViceConnectThread(void *data);
	void connectionThread();

	void updateGetMemory(VICEBinMemGetResponse* resp);

	void handleCheckpointList(VICEBinCheckpointList* cpList);

	void handleCheckpointGet(VICEBinCheckpointResponse* cp);

	void updateRegisters(VICEBinRegisterResponse* resp);

	void handleDisplayGet(VICEBinDisplayResponse* resp);

	void handleStopResume(VICEBinStopResponse* resp);

	void updateRegisterNames(VICEBinRegisterAvailableResponse* resp);

	void close();

	bool open();
	void Tick();
	void AddMessage(uint8_t *message, int size, bool wantResponse = false);

	bool isConnected() { return connected; }
	bool isStopped() { return stopped; }
	void ImWaiting() { waitCount++; }

	IBMutex msgSendMutex;

};

struct GetMemoryRequest {
	uint32_t requestID;
	uint16_t start, end, bank;
	uint8_t space;
};

struct MessageRequestTimeout {
	uint32_t requestID;
	uint32_t timeSincePing;
};

static VI_SOCKET s;
static IBThread threadHandle;
static ViceConnection* viceCon = nullptr;
static uint32_t lastRequestID = 0x0fff;
static std::vector<GetMemoryRequest> sMemRequests;
static std::vector<MessageRequestTimeout> sMessageTimeouts;
static IBMutex userRequestMutex;
static ViceLogger logConsole = nullptr;
static void* logUser = nullptr;
static bool sCloseConnectRequest = false;

static bool sResumeMeansStopped = false;

struct { const char* name; uint8_t id; } aCommandNames[] = {
	{ "MemGet",1 },
	{ "MemSet", 2},
	{ "CheckpointGet", 0x11},
	{ "CheckpointSet", 0x12 },
	{ "CheckpointDelete", 0x13},
	{ "CheckpointList", 0x14},
	{ "CheckpointToggle", 0x15},
	{ "ConditionSet", 0x22 },
	{ "RegistersGet", 0x31 },
	{ "RegistersSet", 0x32},
	{ "Dump", 0x41 },
	{ "Undump", 0x42 },
	{ "ResourceGet", 0x51 },
	{ "ResourceSet", 0x52 },
	{ "JAM", 0x61 },
	{ "Stopped", 0x62 },
	{ "Resumed", 0x63 },
	{ "Step", 0x71 },
	{ "KeyboardFeed", 0x72 },
	{ "StepOut", 0x73 },
	{ "Ping", 0x81 },
	{ "BanksAvailable", 0x82 },
	{ "RegistersAvailable", 0x83 },
	{ "DisplayGet", 0x84 },
	{ "Exit", 0xaa },
	{ "Quit", 0xbb },
	{ "Reset", 0xcc },
	{ "AutoStart", 0xdd }
};



ViceConnection::ViceConnection(const char* ip, uint32_t port) : waitCount(0), ipPort(port), connected(false), stopped(false)
{
	IBMutexInit(&msgSendMutex, "VICE Send Message Mutex");
	strcpy_s(ipAddress, ip);
}


ViceConnection::~ViceConnection()
{
	IBMutexDestroy(&msgSendMutex);
}

void ViceLog(const char* str, size_t len)
{
	strref decap = CaptureVICELine(strref(str, (strl_t)len));
	if (decap && logConsole && logUser) {
		logConsole(logUser, decap.get(), decap.get_len());
	}
}

void ViceLog(strref str)
{
	strref decap = CaptureVICELine(str);
	if (decap && logConsole && logUser) {
		logConsole(logUser, str.get(), str.get_len());
	}
}

void VicePing()
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinHeader pingMsg;
		pingMsg.Setup(0, ++lastRequestID, VICE_Ping);
		viceCon->AddMessage((uint8_t*)&pingMsg, sizeof(VICEBinHeader));
	}
}

bool ViceConnected()
{
	return viceCon && viceCon->isConnected();
}

bool ViceRunning()
{
	return viceCon && viceCon->isConnected() && !viceCon->isStopped();
}

void ViceDisconnect()
{
	if (viceCon && viceCon->isConnected()) {
		sCloseConnectRequest = true;
		VicePing();
	}
}

void ViceQuit()
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinHeader viceQuit;
		viceQuit.Setup(0, ++lastRequestID, VICE_Quit);
		viceCon->AddMessage((uint8_t*)&viceQuit, sizeof(viceQuit));
		VicePing(); // this will fail but will cause the connection thread exit cleanly
	}
}

void ViceBreak()
{
	if (viceCon && viceCon->isConnected() && !viceCon->isStopped()) {
		VICEBinRegisters regMsg(++lastRequestID, false);
		viceCon->AddMessage((uint8_t*)&regMsg, sizeof(regMsg), true);
	}
}

void ViceGo()
{
	ClearBreapointsHit();
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinHeader resumeMsg;
		resumeMsg.Setup(0, ++lastRequestID, VICE_Exit);
		viceCon->AddMessage((uint8_t*)&resumeMsg, sizeof(VICEBinHeader), true);
	}
}

void ViceStep()
{
	ClearBreapointsHit();
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinStep stepMsg;
		stepMsg.Setup(++lastRequestID, false);
		viceCon->AddMessage((uint8_t*)&stepMsg, sizeof(VICEBinStep), true);
		//sResumeMeansStopped = true;
	}
}

void ViceStepOver()
{
	ClearBreapointsHit();
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinStep stepMsg;
		stepMsg.Setup(++lastRequestID, true);
		viceCon->AddMessage((uint8_t*)&stepMsg, sizeof(VICEBinStep), true);
		//sResumeMeansStopped = true;
	}
}

void ViceStepOut()
{
	ClearBreapointsHit();
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinHeader stepOutMsg;
		stepOutMsg.Setup(0, ++lastRequestID, VICE_StepOut);
		viceCon->AddMessage((uint8_t*)&stepOutMsg, sizeof(VICEBinHeader), true);
		//sResumeMeansStopped = true;
	}
}


void ViceRemoveBreakpointNoList(uint32_t number)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinCheckpoint chkpt;
		chkpt.Setup(4, ++lastRequestID, VICE_CheckpointDelete);
		chkpt.SetNumber(number);
		viceCon->AddMessage((uint8_t*)&chkpt, sizeof(chkpt));
	}
}

void ViceRemoveBreakpoint(uint32_t number)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinCheckpoint chkpt;
		chkpt.Setup(4, ++lastRequestID, VICE_CheckpointDelete);
		chkpt.SetNumber(number);
		viceCon->AddMessage((uint8_t*)&chkpt, sizeof(chkpt));
		
		// reset breakpoints
		ClearBreakpoints();
		VICEBinHeader breakList;
		breakList.Setup(0, ++lastRequestID, VICE_CheckpointList);
		viceCon->AddMessage((uint8_t*)&breakList, sizeof(VICEBinHeader));
	}
}

void ViceToggleBreakpoint(uint32_t number, bool enable)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinCheckpointToggle chkpt;
		chkpt.Setup(5, ++lastRequestID, VICE_CheckpointToggle);
		chkpt.SetNumber(number);
		chkpt.enabled = enable ? 1 : 0;
		viceCon->AddMessage((uint8_t*)&chkpt, sizeof(chkpt));

		// reset breakpoints
		ClearBreakpoints();
		VICEBinHeader breakList;
		breakList.Setup(0, ++lastRequestID, VICE_CheckpointList);
		viceCon->AddMessage((uint8_t*)&breakList, sizeof(VICEBinHeader));
	}
}

void ViceAddCheckpoint(uint16_t start, uint16_t end, bool stop, bool load, bool store, bool exec)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinCheckpointSet chkpt;
		chkpt.Setup(8, ++lastRequestID, VICE_CheckpointSet);
		chkpt.SetStart(start);
		chkpt.SetEnd(end);
		chkpt.stopWhenHit = stop ? 1 : 0;
		chkpt.enabled = 1;
		chkpt.operation = (load ? VICE_LoadMem : 0) | (store ? VICE_StoreMem : 0) | (exec ? VICE_Exec : 0);
		chkpt.temporary = 0;
		viceCon->AddMessage((uint8_t*)&chkpt, sizeof(chkpt));

		// reset breakpoints
		ClearBreakpoints();
		VICEBinHeader breakList;
		breakList.Setup(0, ++lastRequestID, VICE_CheckpointList);
		viceCon->AddMessage((uint8_t*)&breakList, sizeof(VICEBinHeader));
	}
}

void ViceAddBreakpoint(uint16_t address)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinCheckpointSet chkpt;
		chkpt.Setup(8, ++lastRequestID, VICE_CheckpointSet);
		chkpt.SetStart(address);
		chkpt.SetEnd(address);
		chkpt.stopWhenHit = 1;
		chkpt.enabled = 1;
		chkpt.operation = VICE_Exec;
		chkpt.temporary = 0;
		viceCon->AddMessage((uint8_t*)&chkpt, sizeof(chkpt));

		// reset breakpoints
		ClearBreakpoints();
		VICEBinHeader breakList;
		breakList.Setup(0, ++lastRequestID, VICE_CheckpointList);
		viceCon->AddMessage((uint8_t*)&breakList, sizeof(VICEBinHeader));
	}
}

void ViceSetCondition(int checkPoint, strref condition)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinSetCondition cond;
		cond.Setup(++lastRequestID, checkPoint, (uint8_t)condition.get_len(), condition.get());
		viceCon->AddMessage((uint8_t*)&cond, sizeof(VICEBinCheckpoint) + 1 + condition.get_len());
	}
}

void ViceRunTo(uint16_t addr)
{
	ClearBreapointsHit();
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinCheckpointSet checkSet;
		checkSet.Setup(8, ++lastRequestID, VICE_CheckpointSet);
		checkSet.SetStart(addr);
		checkSet.SetEnd(addr);
		checkSet.stopWhenHit = true;
		checkSet.enabled = true;
		checkSet.operation = (uint8_t)VICE_Exec;
		checkSet.temporary = true;
		viceCon->AddMessage((uint8_t*)&checkSet, sizeof(checkSet), true);
		ViceGo();
	}
}

void ViceStartProgram(const char* loadPrg)
{
	if (viceCon && viceCon->isConnected()) {
		size_t loadFileLen = strlen(loadPrg);
		VICEBinAutoStart autoStart;
		autoStart.Setup((uint32_t)loadFileLen + 4, ++lastRequestID, VICE_AutoStart);
		autoStart.startImmediately = 1;
		autoStart.fileIndex[0] = 0;
		autoStart.fileIndex[1] = 0;
		autoStart.fileNameLength = (uint8_t)loadFileLen;
		memcpy(autoStart.filename, loadPrg, loadFileLen);
		viceCon->AddMessage((uint8_t*)&autoStart, (uint32_t)(sizeof(VICEBinHeader) + 4 + loadFileLen), true);
	}
}

void ViceReset(uint8_t resetType)
{
	if (viceCon && viceCon->isConnected()) {
		VICEBinReset reset;
		reset.Setup(1, ++lastRequestID, VICE_Reset);
		reset.resetType = resetType;
		viceCon->AddMessage((uint8_t*)&reset, sizeof(VICEBinReset), false);
	}
}


void ViceWaiting()
{
	if (viceCon && viceCon->isConnected()) {
		viceCon->ImWaiting();
	}
}

void ViceTickMessage()
{
	if (viceCon) { viceCon->Tick(); }
}

static const int numNames = sizeof(aCommandNames) / sizeof(aCommandNames[0]);
const char* ViceBinCmdName(uint8_t cmd)
{
	for (int i = 0, n = numNames; i < n; ++i) {
		if (aCommandNames[i].id == cmd) { return aCommandNames[i].name; }
	}
	return "?";
}

#define MAX_REGS_BUF 64

bool ViceSetRegisters(const CPU6510& cpu, uint32_t regMask)
{
	if (viceCon) {
		const CPU6510::Regs& regs = cpu.regs;
		VICEMemSpaces mem = cpu.space;

		uint8_t msgRaw[MAX_REGS_BUF + sizeof(VICEBinRegisterSet)];
		VICEBinRegisterSet* rm = (VICEBinRegisterSet*)msgRaw;
		VICEBinRegisterSetSingle* ri = rm->regs;
		uint32_t size = 3;	// memSpace + count[2]
		uint16_t count = 0;
		if (regMask & CPU6510::RM_A) {
			ri->itemSize = 3;
			ri->regID = VICE_Acc;
			ri->value[0] = regs.A;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_X) {
			ri->itemSize = 3;
			ri->regID = VICE_X;
			ri->value[0] = regs.X;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_Y) {
			ri->itemSize = 3;
			ri->regID = VICE_Y;
			ri->value[0] = regs.Y;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_SP) {
			ri->itemSize = 3;
			ri->regID = VICE_SP;
			ri->value[0] = regs.SP;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_FL) {
			ri->itemSize = 3;
			ri->regID = VICE_FL;
			ri->value[0] = regs.FL;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_ZP00) {
			ri->itemSize = 3;
			ri->regID = VICE_00;
			ri->value[0] = regs.ZP00;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_ZP01) {
			ri->itemSize = 3;
			ri->regID = VICE_01;
			ri->value[0] = regs.ZP01;
			size += 4;
			++ri;
			++count;
		}
		if (regMask & CPU6510::RM_PC) {
			ri->itemSize = 3;
			ri->regID = VICE_PC;
			ri->value[0] = (uint8_t)regs.PC;
			ri->value[1] = (uint8_t)(regs.PC >> 8);
			size += 4;
			++ri;// = (VICEBinRegisterSetSingle*)((uint8_t*)ri + 4);
			++count;
		}
		rm->Setup(size, ++lastRequestID, VICE_RegistersSet);
		rm->memSpace = (uint8_t)mem;
		rm->count[0] = (uint8_t)count;
		rm->count[1] = (uint8_t)(count >> 8);
		viceCon->AddMessage((uint8_t*)rm, size + sizeof(VICEBinHeader), true);
		return true;
	}
	return false;
}


bool ViceGetMemory(uint16_t start, uint16_t end, VICEMemSpaces mem)
{
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinMemGetSet getNem(++lastRequestID, false, true, start, end, 0, mem);
		GetMemoryRequest reqInfo = { lastRequestID, start, end, 0, (uint8_t)mem };
		IBMutexLock(&userRequestMutex);
		sMemRequests.push_back(reqInfo);
		IBMutexRelease(&userRequestMutex);
#ifdef VICELOG
		strown<128> msg("Requested VICE Memory $");
		msg.append_num(start, 4, 16).append("-$").append_num(end, 4, 16).append("\n");
		ViceLog(msg.get_strref());
		OutputDebugStringA(msg.c_str());
#endif
		viceCon->AddMessage((uint8_t*)&getNem, sizeof(getNem), true);
		return true;
	}
	return false;
}

bool ViceSetMemory(uint16_t start, uint16_t len, uint8_t* bytes, VICEMemSpaces mem)
{
	if (viceCon && viceCon->isConnected() && viceCon->isStopped()) {
		VICEBinMemGetSet* setMem = (VICEBinMemGetSet*)calloc(1, sizeof(VICEBinMemGetSet) + len);
		setMem->Setup(++lastRequestID, false, false, start, start + len - 1, 0, mem);
		memcpy(setMem + 1, bytes, len);
#ifdef VICELOG
		strown<128> msg("Setting VICE Memory $");
		msg.append_num(start, 4, 16).append("-$").append_num(start + len - 1, 4, 16).append("\n");
		ViceLog(msg.get_strref());
		OutputDebugStringA(msg.c_str());
#endif
		viceCon->AddMessage((uint8_t*)setMem, sizeof(VICEBinMemGetSet) + len, true);
		return true;
	}
	return false;
}

void ViceAddLogger(ViceLogger logger, void* user)
{
	logConsole = logger;
	logUser = user;
}

void ViceConnection::connectionThread()
{
	char* recvBuf = (char*)malloc(RECEIVE_SIZE);
	if (!recvBuf) { return; }

	// Open the connection
	if (!open()) { return; }

	int32_t timeout = 100000;// SOCKET_READ_TIMEOUT_SEC*1000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO | SO_DEBUG, (char*)&timeout, sizeof(timeout));


	bool activeConnection = true;
	{
//		VICEBinRegisters regAvailMsg(++lastRequestID, true);
//		VICEBinRegisters regMsg(++lastRequestID, false);
//		AddMessage((uint8_t*)&regMsg, sizeof(regMsg));
//		AddMessage((uint8_t*)&regAvailMsg, sizeof(regAvailMsg));
//		AddMessage((uint8_t*)&regMsg, sizeof(regMsg));
	}

	size_t bufferRead = 0;
	connected = true;

	while (activeConnection) {
	// close after all commands have been sent?
		if (sCloseConnectRequest) {
			sCloseConnectRequest = false;
		#ifdef _WIN32
			threadHandle = INVALID_HANDLE_VALUE;
		#endif
			close();
			break;
		}

		// messages to receive
		assert(bufferRead < (RECEIVE_SIZE * 3 / 4));
		int bytesReceived = recv(s, recvBuf + bufferRead, int(RECEIVE_SIZE - bufferRead), 0);
		if (bytesReceived == SOCKET_ERROR) {
		#ifdef _WIN32
			if (WSAGetLastError() == WSAETIMEDOUT) {
//				if ((state == Vice_None || state == Vice_Running) && stopRequest) {
//					offs = 0;
//					send(s, "r\n", 2, NULL);
//					stopRequest = false;
//				} else if (syncRequest) {
//					syncRequest = false;
//					ClearAllPCBreakpoints();
//					ResetViceBP();
//					send(s, sBundle, (int)strlen(sBundle), NULL);
//					state = Vice_Sync;
//					syncing = true;
//					monitorOn = true;
//					offs = 0;
//					viceRunning = false;
//					viceReloadSymbols = true;
//				}
				Sleep(50);
			} else {
				activeConnection = false;
				break;
			}
		#else
			Sleep(50);
		#endif
		} else {
			bufferRead += bytesReceived;

			while (bufferRead >= sizeof(VICEBinResponse) && recvBuf[0] == 2) {
				VICEBinResponse* resp = (VICEBinResponse*)recvBuf;
				uint32_t bytes = resp->GetSize();
				if (bufferRead >= bytes) {
#ifdef VICELOG
					strown<128> msg("Got resp: $");
					msg.append_num(resp->commandType, 2, 16);
					msg.append(" (").append(ViceBinCmdName(resp->commandType)).append(")");
					msg.append(" ReqID:").append_num(resp->GetReqID(), 0, 16);
					if (resp->errorCode) {
						msg.append(" err: ").append_num(resp->errorCode, 2, 16);
					}
					msg.append("\n");
					ViceLog(msg.get_strref());
					OutputDebugStringA(msg.c_str());
#endif
					uint32_t id = resp->GetReqID();
					if (id != 0xffffffff) {
						IBMutexLock(&msgSendMutex);
						for (size_t i = 0, n = sMessageTimeouts.size(); i < n; ++i) {
							if (sMessageTimeouts[i].requestID == id) {
								sMessageTimeouts.erase(sMessageTimeouts.begin() + i);
								break;
							}
						}
						IBMutexRelease(&msgSendMutex);
					}

					switch (resp->commandType) {
						case VICE_RegistersGet:
							updateRegisters((VICEBinRegisterResponse*)resp);
							break;
						case VICE_RegistersAvailable:
							updateRegisterNames((VICEBinRegisterAvailableResponse*)resp);
							break;
						case VICE_Resumed:
#ifdef _DEBUG
							OutputDebugStringA("Vice resumed\n");
#endif
							handleStopResume((VICEBinStopResponse*)resp);
							break;
						case VICE_MemGet:
							updateGetMemory((VICEBinMemGetResponse*)resp);
							break;
						case VICE_CheckpointList:
							handleCheckpointList((VICEBinCheckpointList*)resp);
							break;
						case VICE_CheckpointGet:
							handleCheckpointGet((VICEBinCheckpointResponse*)resp);
							break;
						case VICE_Step:
#ifdef _DEBUG
							OutputDebugStringA("Vice stepped!\n");
#endif
							break;
						case VICE_Stopped:
						case VICE_JAM:
#ifdef _DEBUG
							OutputDebugStringA("Vice stopped\n");
#endif
							handleStopResume((VICEBinStopResponse*)resp);
							break;
						case VICE_DisplayGet:
							handleDisplayGet((VICEBinDisplayResponse*)resp);
							break;
						case VICE_AutoStart:
#ifdef _DEBUG
							OutputDebugStringA("Loaded!\n");
#endif
							break;
					}
					if (bufferRead > bytes) {
						memmove(recvBuf, recvBuf + bytes, bufferRead - bytes);
						bufferRead -= bytes;
					} else {
						bufferRead = 0;
					}

				} else {
					break;
				}
			}
		}

#ifndef SEND_IMMEDIATE
		// messages to send
		{
			ViceMessage* msg = nullptr;
			IBMutexLock(&msgSendMutex);
			if (toSend.size()) {
				msg = toSend.front(); toSend.pop();
			}
			IBMutexRelease(&msgSendMutex);
			if (msg) {
				send(s, (const char*)(&msg->size + 1), msg->size, 0);
				free(msg);
			}
		}
#endif
	}
	// connection with VICE was terminated for some reason
	free(recvBuf);
	IBMutexLock(&msgSendMutex);
	sMessageTimeouts.clear();
	sMemRequests.clear();
	connected = false;
	IBMutexRelease(&msgSendMutex);
}

void ViceConnection::updateGetMemory(VICEBinMemGetResponse* resp)
{
	IBMutexLock(&userRequestMutex);
	// TODO: Check memory range for end
	uint32_t id = resp->GetReqID();
	uint16_t start = 0, bank = 0;
	uint8_t space = 0;
	bool found = false;
	for (size_t i = 0; i < sMemRequests.size(); ++i) {
		if (sMemRequests[i].requestID == id) {
			start = sMemRequests[i].start;
			//end = sMemRequests[i].end;
			bank = sMemRequests[i].bank;
			space = sMemRequests[i].space;
			found = true;
			sMemRequests.erase(sMemRequests.begin() + i);
			break;
		}
	}
	IBMutexRelease(&userRequestMutex);

	assert(found);
	if (CPU6510* cpu = GetCPU((VICEMemSpaces)space)) {
#ifdef VICELOG
		strown<128> msg("updating $");
		msg.append_num(start, 4, 16).append("-$").append_num(start + resp->bytes[0] + (((uint16_t)resp->bytes[1]) << 8) - 1, 4, 16);
		msg.append(" mem/bank:").append_num(space, 0, 10).append("/").append_num(bank, 0, 10);
		ViceLog(msg.get_strref());
#endif
		cpu->MemoryFromVICE(start, start + resp->bytes[0] + (((uint16_t)resp->bytes[1]) << 8) - 1, resp->data);
	}
}

void ViceConnection::handleCheckpointList(VICEBinCheckpointList* cpList)
{
#ifdef _DEBUG
	strown<32> msg;
	msg.sprintf("%d checkpoints found", cpList->GetCount());
	ViceLog(msg);
#endif
}

// this also gets called every tracepoint!
void ViceConnection::handleCheckpointGet(VICEBinCheckpointResponse* cp)
{
	uint32_t flags = 0;
	if (cp->enabled) flags |= Breakpoint::Enabled;
	if (cp->stopWhenHit) flags |= Breakpoint::Stop;
	if (cp->operation & VICE_Exec) flags |= Breakpoint::Exec;
	if (cp->operation & VICE_LoadMem) flags |= Breakpoint::Load;
	if (cp->operation & VICE_StoreMem) flags |= Breakpoint::Store;
	if (cp->wasHit) {
		flags |= Breakpoint::Current;
		SetBreakpointHit(cp->GetNumber());
	}
	if (cp->temporary) flags |= Breakpoint::Temporary;
	AddBreakpoint(cp->GetNumber(), flags, cp->GetStart(), cp->GetEnd(),
				  cp->hasCondition ? "yes" : nullptr);
	if (cp->hasCondition) {
		// no command for receiving checkpoint condition?
	}
}

void ViceConnection::updateRegisters(VICEBinRegisterResponse* resp)
{
	if (CPU6510* cpu = GetMainCPU()) {
		for (uint16_t r = 0, n = resp->GetCount(); r < n; ++r) {
			VICEBinRegisterResponse::regInfo& info = resp->aRegs[r];
			switch (info.registerID) {
				case VICE_Acc: cpu->regs.A = info.GetValue8(); break;
				case VICE_X: cpu->regs.X = info.GetValue8(); break;
				case VICE_Y: cpu->regs.Y = info.GetValue8(); break;
				case VICE_PC: cpu->regs.PC = info.GetValue16(); break;
				case VICE_SP: cpu->regs.SP = info.GetValue8(); break;
				case VICE_FL: cpu->regs.FL = info.GetValue8(); break;
				case VICE_LIN: cpu->regs.LIN = info.GetValue16(); break;
				case VICE_CYC: cpu->regs.CYC = info.GetValue16(); break;
				case VICE_00: cpu->regs.ZP00 = info.GetValue8(); break;
				case VICE_01: cpu->regs.ZP01 = info.GetValue8(); break;
			}
		}
	}
}

void ViceConnection::handleDisplayGet(VICEBinDisplayResponse* resp)
{
	RefreshScreen(resp->image, resp->GetWidthImage(), resp->GetHeightImage(),
		resp->GetLeftScreen(), resp->GetTopScreen(),
		resp->GetWidthScreen(), resp->GetHeightScreen());
}

void ViceConnection::handleStopResume(VICEBinStopResponse* resp)
{
#ifdef VICELOG
	strown<256> msg("Stop/Resume PC=$");
	msg.append_num(resp->GetPC(), 4, 16);
	ViceLog(msg.get_strref());
#endif
	if (CPU6510* cpu = GetCurrCPU()) {
		cpu->regs.PC = resp->GetPC();
	}
	switch (resp->commandType) {
		case VICE_Resumed:
			stopped = false;
			break;
		case VICE_Stopped:
		case VICE_JAM: {
			stopped = true;
			ViceGetMemory(0x0000, 0x7fff, VICEMemSpaces::MainMemory);
			ViceGetMemory(0x8000, 0xffff, VICEMemSpaces::MainMemory);

			// breakpoint list is just an empty message
			ClearBreakpoints();
			VICEBinHeader breakList;
			breakList.Setup(0, ++lastRequestID, VICE_CheckpointList);
			AddMessage((uint8_t*)&breakList, sizeof(VICEBinHeader));

			// update the vice display
			// TODO: skip if ScreenView is hidden
			VICEBinDisplay getDisplay(++lastRequestID, VICEDisplay_Indexed);
			AddMessage((uint8_t*)&getDisplay, sizeof(VICEBinDisplay));

			break;
		}
	}
	sResumeMeansStopped = false;
}

void ViceConnection::updateRegisterNames(VICEBinRegisterAvailableResponse* resp)
{
	VICEBinRegisterAvailableResponse::regInfo* info = &resp->aRegs;
	for (uint16_t r = 0, n = resp->GetCount(); r < n; ++r) {
#ifdef _DEBUG
		strown<256> d;
		d.append("Reg: $").append_num(info->registerID, 2, 16);
		d.append(" Size: ").append_num(info->registerSize, 0, 10);
		d.append(" Bits: ").append_num(info->registerBits, 0, 10);
		strref name((const char*)info->registerName, info->registerNameLen);
		d.append(name).append('\n');
		OutputDebugStringA(d.c_str());
#endif
		info = (VICEBinRegisterAvailableResponse::regInfo *)
			((uint8_t*)info + sizeof(VICEBinRegisterAvailableResponse::regInfo) +
			 info->registerNameLen - 1);
	}

}

void ViceConnection::close()
{
	IBDestroyThread(&threadHandle);
#ifdef _WIN32
	closesocket(s);
	WSACleanup();
#else
	shutdown(s, SHUT_RDWR);
#endif
	sCloseConnectRequest = false;
}

bool ViceConnection::open()
{
#ifdef _WIN32	
	WSADATA ws;
	long status = WSAStartup(0x0101, &ws);
	if (status != 0) { return false; }
#endif

	s = socket(AF_INET, SOCK_STREAM, 0);

	// Make sure the user has specified a port
	if (ipPort < 0 || ipPort > 65535) { return false; }

	int32_t iResult = 0;
	int32_t dwRetval;
	sockaddr_in saGNI;
	char hostname[NI_MAXHOST];
	char servInfo[NI_MAXSERV];
	saGNI.sin_family = AF_INET;
	inet_pton(AF_INET, ipAddress, &(saGNI.sin_addr.s_addr));
	saGNI.sin_port = htons(ipPort);
	dwRetval = getnameinfo((struct sockaddr*)&saGNI,
						   sizeof(struct sockaddr),
						   hostname,
						   NI_MAXHOST, servInfo, NI_MAXSERV, NI_NUMERICSERV);

	if (dwRetval != 0) {
		return false;
	}

	iResult = ::connect(s, (struct sockaddr*)&saGNI, sizeof(saGNI));
	sCloseConnectRequest = false;
	return iResult == 0;
}

void ViceConnection::Tick()
{
	IBMutexLock(&msgSendMutex);
	uint32_t maxTime = 0;
	for (size_t i = 0, n = sMessageTimeouts.size(); i < n; ++i) {
		sMessageTimeouts[i].timeSincePing++;
		if (sMessageTimeouts[i].timeSincePing > maxTime) {
			maxTime = sMessageTimeouts[i].timeSincePing;
		}
	}
	IBMutexRelease(&msgSendMutex);
	if (maxTime > 100) {
#ifdef VICELOG
		strown<256> msg("No response for:");
		IBMutexLock(&msgSendMutex);
//		uint32_t maxTime = 0;
		for (size_t i = 0, n = sMessageTimeouts.size(); i < n; ++i) {
			if (i) { msg.append(", "); }
			msg.append_num(sMessageTimeouts[i].requestID, 0, 16);
		}
		IBMutexRelease(&msgSendMutex);
		msg.append(". Sending Ping to VICE");
		ViceLog(msg.get_strref());
#endif
		VicePing();
		IBMutexLock(&msgSendMutex);
		for (size_t i = 0, n = sMessageTimeouts.size(); i < n; ++i) {
			sMessageTimeouts[i].timeSincePing = 0;
		}
		IBMutexRelease(&msgSendMutex);
	}
}

void ViceConnection::AddMessage(uint8_t* message, int size, bool wantResponse)
{
#ifdef VICELOG
	VICEBinHeader* hdr = (VICEBinHeader*)message;
	strown<128> str("Send cmd: $");
	str.append_num(hdr->commandType, 2, 16).append(" (").append(ViceBinCmdName(hdr->commandType));
	str.append(") ReqID: ").append_num(hdr->GetReqID(), 0, 16);
	str.append(" len: ").append_num(hdr->GetSize(), 0, 16).append(" / ").append_num(size, 0, 16).append("\n");
	ViceLog(str.get_strref());
	OutputDebugStringA(str.c_str());
#endif

#ifdef SEND_IMMEDIATE
	if (wantResponse) {
		MessageRequestTimeout to = { ((VICEBinHeader*)message)->GetReqID(), 0 };
		IBMutexLock(&msgSendMutex);
		sMessageTimeouts.push_back(to);
		IBMutexRelease(&msgSendMutex);
	}
	send(s, (const char*)message, size, 0);
#else

	ViceMessage* msg = (ViceMessage*)malloc(sizeof(ViceMessage) + size);
	if (msg) {
		if (wantResponse) {
			MessageRequestTimeout to = { ((VICEBinHeader*)message)->GetReqID(), 0 };
			sMessageTimeouts.push_back(to);
		}
		msg->size = size;
		memcpy(&msg->size + 1, message, size);
		IBMutexLock(&msgSendMutex);
		toSend.push(msg);
		IBMutexRelease(&msgSendMutex);
	}
#endif
}

IBThreadRet WINAPI ViceConnection::ViceConnectThread(void* data)
{
	IBMutexInit(&userRequestMutex, "User request VICE operations");
	((ViceConnection*)data)->connectionThread();
	IBMutexDestroy(&userRequestMutex);
	if ((void*)viceCon == data) {
		viceCon = nullptr;
		delete (ViceConnection*)data;
	}
	return 0;
}


void ViceConnect(const char* ip, uint32_t port)
{
	if (viceCon != nullptr) {
		return;	// already going
	}

	viceCon = new ViceConnection(ip, port);
	if (viceCon == nullptr) {
		return;	// couldn't create
	}

	IBCreateThread(&threadHandle, 16384, ViceConnection::ViceConnectThread, viceCon);
};
