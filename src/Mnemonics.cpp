#include <stdlib.h>
#include "struse/struse.h"
#include "6510.h"
#include "Mnemonics.h"
#include "Sym.h"

#ifndef _WIN32
#define _strnicmp strncasecmp
#endif

static const char* aAddrModeFmt[] = {
	"($%02x,x)",		// 00
	"$%02x",			// 01
	"#$%02x",			// 02
	"$%04x",			// 03
	"($%02x),y",		// 04
	"$%02x,x",			// 05
	"$%04x,y",			// 06
	"$%04x,x",			// 07
	"($%04x)",			// 08
	"A",				// 09
	"",					// 0a
	"$%04x",			// 1a
	"($%02x,y)",		// 17
	"$%02x,y",			// 16
};

static const char* aAddrModeLblFmt[] = {
	"(%s,x) ; $%02x",	// 00
	"%s ; $%02x",		// 01
	"#%s ; $%02x",		// 02
	"%s ; $%04x",		// 03
	"(%s),y ; $%02x",	// 04
	"%s,x ; $%02x",		// 05
	"%s,y ; $%04x",		// 06
	"%s,x ; $%04x",		// 07
	"(%s) ; $%04x",		// 08
	"A",				// 09
	"",					// 0a
	"%s ; $%04x",		// 1a
	"(%s,y) ; %02x",	// 17
	"%s,y ; %02x" ,		// 16
};

const char* AddressModeNames[]{
	// address mode bit index

	// 6502

	"AM_ZP_REL_X",	// 0 ($12",x)
	"AM_ZP",			// 1 $12
	"AM_IMM",			// 2 #$12
	"AM_ABS",			// 3 $1234
	"AM_ZP_Y_REL",	// 4 ($12)",y
	"AM_ZP_X",		// 5 $12",x
	"AM_ABS_Y",		// 6 $1234",y
	"AM_ABS_X",		// 7 $1234",x
	"AM_REL",			// 8 ($1234)
	"AM_ACC",			// 9 A
	"AM_NON",			// a
	"AM_BRANCH",
	"AM_ZP_REL_Y",
	"AM_ZP_Y",
};

enum MNM_Base {
	mnm_brk,
	mnm_ora,
	mnm_cop,
	mnm_tsb,
	mnm_asl,
	mnm_php,
	mnm_phd,
	mnm_bpl,
	mnm_trb,
	mnm_clc,
	mnm_inc,
	mnm_tcs,
	mnm_jsr,
	mnm_and,
	mnm_bit,
	mnm_rol,
	mnm_plp,
	mnm_pld,
	mnm_bmi,
	mnm_sec,
	mnm_dec,
	mnm_tsc,
	mnm_rti,
	mnm_eor,
	mnm_wdm,
	mnm_mvp,
	mnm_lsr,
	mnm_pha,
	mnm_phk,
	mnm_jmp,
	mnm_bvc,
	mnm_mvn,
	mnm_cli,
	mnm_phy,
	mnm_tcd,
	mnm_rts,
	mnm_adc,
	mnm_per,
	mnm_stz,
	mnm_ror,
	mnm_rtl,
	mnm_bvs,
	mnm_sei,
	mnm_ply,
	mnm_tdc,
	mnm_bra,
	mnm_sta,
	mnm_brl,
	mnm_sty,
	mnm_stx,
	mnm_dey,
	mnm_txa,
	mnm_phb,
	mnm_bcc,
	mnm_tya,
	mnm_txs,
	mnm_txy,
	mnm_ldy,
	mnm_lda,
	mnm_ldx,
	mnm_tay,
	mnm_tax,
	mnm_plb,
	mnm_bcs,
	mnm_clv,
	mnm_tsx,
	mnm_tyx,
	mnm_cpy,
	mnm_cmp,
	mnm_rep,
	mnm_iny,
	mnm_dex,
	mnm_wai,
	mnm_bne,
	mnm_pei,
	mnm_cld,
	mnm_phx,
	mnm_stp,
	mnm_cpx,
	mnm_sbc,
	mnm_sep,
	mnm_inx,
	mnm_nop,
	mnm_xba,
	mnm_beq,
	mnm_pea,
	mnm_sed,
	mnm_plx,
	mnm_xce,
	mnm_inv,
	mnm_pla,

	mnm_wdc_and_illegal_instructions,

	mnm_bbs0 = mnm_wdc_and_illegal_instructions,
	mnm_bbs1,
	mnm_bbs2,
	mnm_bbs3,
	mnm_bbs4,
	mnm_bbs5,
	mnm_bbs6,
	mnm_bbs7,
	mnm_bbr0,
	mnm_bbr1,
	mnm_bbr2,
	mnm_bbr3,
	mnm_bbr4,
	mnm_bbr5,
	mnm_bbr6,
	mnm_bbr7,

	mnm_ahx,
	mnm_anc,
	mnm_aac,
	mnm_alr,
	mnm_axs,
	mnm_dcp,
	mnm_isc,
	mnm_lax,
	mnm_lax2,
	mnm_rla,
	mnm_rra,
	mnm_sre,
	mnm_sax,
	mnm_slo,
	mnm_xaa,
	mnm_arr,
	mnm_tas,
	mnm_shy,
	mnm_shx,
	mnm_las,
	mnm_sbi,

	mnm_count
};

const char* zsMNM[mnm_count]{
	"brk",
	"ora",
	"cop",
	"tsb",
	"asl",
	"php",
	"phd",
	"bpl",
	"trb",
	"clc",
	"inc",
	"tcs",
	"jsr",
	"and",
	"bit",
	"rol",
	"plp",
	"pld",
	"bmi",
	"sec",
	"dec",
	"tsc",
	"rti",
	"eor",
	"wdm",
	"mvp",
	"lsr",
	"pha",
	"phk",
	"jmp",
	"bvc",
	"mvn",
	"cli",
	"phy",
	"tcd",
	"rts",
	"adc",
	"per",
	"stz",
	"ror",
	"rtl",
	"bvs",
	"sei",
	"ply",
	"tdc",
	"bra",
	"sta",
	"brl",
	"sty",
	"stx",
	"dey",
	"txa",
	"phb",
	"bcc",
	"tya",
	"txs",
	"txy",
	"ldy",
	"lda",
	"ldx",
	"tay",
	"tax",
	"plb",
	"bcs",
	"clv",
	"tsx",
	"tyx",
	"cpy",
	"cmp",
	"rep",
	"iny",
	"dex",
	"wai",
	"bne",
	"pei",
	"cld",
	"phx",
	"stp",
	"cpx",
	"sbc",
	"sep",
	"inx",
	"nop",
	"xba",
	"beq",
	"pea",
	"sed",
	"plx",
	"xce",
	"???",
	"pla",
	"bbs0",
	"bbs1",
	"bbs2",
	"bbs3",
	"bbs4",
	"bbs5",
	"bbs6",
	"bbs7",
	"bbr0",
	"bbr1",
	"bbr2",
	"bbr3",
	"bbr4",
	"bbr5",
	"bbr6",
	"bbr7",
	"ahx",
	"anc",
	"aac",
	"alr",
	"axs",
	"dcp",
	"isc",
	"lax",
	"lax2",
	"rla",
	"rra",
	"sre",
	"sax",
	"slo",
	"xaa",
	"arr",
	"tas",
	"shy",
	"shx",
	"las",
	"sbi",
};

struct dismnm {
	MNM_Base mnemonic;
	unsigned char addrMode;
	unsigned char arg_size;
	InstrRefType ref_type;
};

struct dismnm a6502_ops[256] = {
	{ mnm_brk, AM_NON, 0, InstrRefType::None },
	{ mnm_ora, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_slo, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ora, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_asl, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_slo, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_php, AM_NON, 0, InstrRefType::Register },
	{ mnm_ora, AM_IMM, 1, InstrRefType::Register },
	{ mnm_asl, AM_NON, 0, InstrRefType::Register },
	{ mnm_anc, AM_IMM, 1, InstrRefType::Register },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ora, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_asl, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_slo, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bpl, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_ora, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_slo, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ora, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_asl, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_slo, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_clc, AM_NON, 0, InstrRefType::Flags },
	{ mnm_ora, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_slo, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ora, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_asl, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_slo, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_jsr, AM_ABS, 2, InstrRefType::Code },
	{ mnm_and, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rla, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_bit, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_and, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_rol, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_rla, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_plp, AM_NON, 0, InstrRefType::Register },
	{ mnm_and, AM_IMM, 1, InstrRefType::Register },
	{ mnm_rol, AM_NON, 0, InstrRefType::Register },
	{ mnm_aac, AM_IMM, 1, InstrRefType::Register },
	{ mnm_bit, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_and, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_rol, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_rla, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bmi, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_and, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rla, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_and, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_rol, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_rla, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_sec, AM_NON, 0, InstrRefType::Flags },
	{ mnm_and, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rla, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_and, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_rol, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_rla, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_rti, AM_NON, 0, InstrRefType::None },
	{ mnm_eor, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sre, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_eor, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_lsr, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_sre, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_pha, AM_NON, 0, InstrRefType::Register },
	{ mnm_eor, AM_IMM, 1, InstrRefType::Register },
	{ mnm_lsr, AM_NON, 0, InstrRefType::Register },
	{ mnm_alr, AM_IMM, 1, InstrRefType::Register },
	{ mnm_jmp, AM_ABS, 2, InstrRefType::Code },
	{ mnm_eor, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_lsr, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_sre, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bvc, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_eor, AM_ZP_Y_REL, 1, InstrRefType::DataValue },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sre, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_eor, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_lsr, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_sre, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_cli, AM_NON, 0, InstrRefType::Flags },
	{ mnm_eor, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sre, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_eor, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_lsr, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_sre, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_rts, AM_NON, 0, InstrRefType::None },
	{ mnm_adc, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rra, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_adc, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_ror, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_rra, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_pla, AM_NON, 0, InstrRefType::Register },
	{ mnm_adc, AM_IMM, 1, InstrRefType::Register },
	{ mnm_ror, AM_NON, 0, InstrRefType::Register },
	{ mnm_arr, AM_IMM, 1, InstrRefType::Register },
	{ mnm_jmp, AM_REL, 2, InstrRefType::Code },
	{ mnm_adc, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_ror, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_rra, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bvs, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_adc, AM_ZP_Y_REL, 1, InstrRefType::DataValue },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rra, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_adc, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_ror, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_rra, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_sei, AM_NON, 0, InstrRefType::Flags },
	{ mnm_adc, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_rra, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_adc, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_ror, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_rra, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sta, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sax, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_sty, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_sta, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_stx, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_sax, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_dey, AM_NON, 0, InstrRefType::Register },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_txa, AM_NON, 0, InstrRefType::Register },
	{ mnm_xaa, AM_IMM, 1, InstrRefType::Register },
	{ mnm_sty, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_sta, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_stx, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_sax, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bcc, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_sta, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ahx, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_sty, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_sta, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_stx, AM_ZP_Y, 1, InstrRefType::DataArray },
	{ mnm_sax, AM_ZP_Y, 1, InstrRefType::DataArray },
	{ mnm_tya, AM_NON, 0, InstrRefType::Register },
	{ mnm_sta, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_txs, AM_NON, 0, InstrRefType::Register },
	{ mnm_tas, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_shy, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_sta, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_shx, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_ahx, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_ldy, AM_IMM, 1, InstrRefType::Register },
	{ mnm_lda, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_ldx, AM_IMM, 1, InstrRefType::Register },
	{ mnm_lax, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_ldy, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_lda, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_ldx, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_lax, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_tay, AM_NON, 0, InstrRefType::Register },
	{ mnm_lda, AM_IMM, 1, InstrRefType::Register },
	{ mnm_tax, AM_NON, 0, InstrRefType::Register },
	{ mnm_lax2, AM_IMM, 1, InstrRefType::Register },
	{ mnm_ldy, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_lda, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_ldx, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_lax, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bcs, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_lda, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_ldy, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_lda, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_ldx, AM_ZP_Y, 1, InstrRefType::DataArray },
	{ mnm_lax, AM_ZP_Y, 1, InstrRefType::DataArray },
	{ mnm_clv, AM_NON, 0, InstrRefType::Flags },
	{ mnm_lda, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_tsx, AM_NON, 0, InstrRefType::Register },
	{ mnm_las, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_ldy, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_lda, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_ldx, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_lax, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_cpy, AM_IMM, 1, InstrRefType::Register },
	{ mnm_cmp, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_dcp, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_cpy, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_cmp, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_dec, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_dcp, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_iny, AM_NON, 0, InstrRefType::Register },
	{ mnm_cmp, AM_IMM, 1, InstrRefType::Register },
	{ mnm_dex, AM_NON, 0, InstrRefType::Register },
	{ mnm_axs, AM_IMM, 1, InstrRefType::Register },
	{ mnm_cpy, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_cmp, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_dec, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_dcp, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_bne, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_cmp, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_dcp, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_cmp, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_dec, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_dcp, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_cld, AM_NON, 0, InstrRefType::Flags },
	{ mnm_cmp, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_dcp, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_cmp, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_dec, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_dcp, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_cpx, AM_IMM, 1, InstrRefType::Register },
	{ mnm_sbc, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_isc, AM_ZP_REL_X, 1, InstrRefType::DataArray },
	{ mnm_cpx, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_sbc, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_inc, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_isc, AM_ZP, 1, InstrRefType::DataValue },
	{ mnm_inx, AM_NON, 0, InstrRefType::Register },
	{ mnm_sbc, AM_IMM, 1, InstrRefType::Register },
	{ mnm_nop, AM_NON, 0, InstrRefType::None },
	{ mnm_sbi, AM_IMM, 1, InstrRefType::Register },
	{ mnm_cpx, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_sbc, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_inc, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_isc, AM_ABS, 2, InstrRefType::DataValue },
	{ mnm_beq, AM_BRANCH, 1, InstrRefType::Code },
	{ mnm_sbc, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_isc, AM_ZP_Y_REL, 1, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sbc, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_inc, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_isc, AM_ZP_X, 1, InstrRefType::DataArray },
	{ mnm_sed, AM_NON, 0, InstrRefType::Flags },
	{ mnm_sbc, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_isc, AM_ABS_Y, 2, InstrRefType::DataArray },
	{ mnm_inv, AM_NON, 0, InstrRefType::None },
	{ mnm_sbc, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_inc, AM_ABS_X, 2, InstrRefType::DataArray },
	{ mnm_isc, AM_ABS_X, 2, InstrRefType::DataArray },
};

int InstructionBytes(CPU6510* cpu, uint16_t addr, bool illegals)
{
	const dismnm* opcodes = a6502_ops;
	unsigned char op = cpu->GetByte(addr);
	bool not_valid = opcodes[op].mnemonic == mnm_inv || (!illegals && opcodes[op].mnemonic >= mnm_wdc_and_illegal_instructions);
	int arg_size = not_valid ? 0 : opcodes[op].arg_size;

	return arg_size + 1;
}

int ValidInstructionBytes(CPU6510* cpu, uint16_t addr, bool illegals)
{
	const dismnm* opcodes = a6502_ops;
	unsigned char op = cpu->GetByte(addr);
	bool not_valid = opcodes[op].mnemonic == mnm_inv || (!illegals && opcodes[op].mnemonic >= mnm_wdc_and_illegal_instructions);
	return not_valid ? 0 : (opcodes[op].arg_size + 1);
}

InstrRefType GetRefType(CPU6510* cpu, uint16_t addr) {
	const dismnm* opcodes = a6502_ops;
	return opcodes[cpu->GetByte(addr)].ref_type;
}

// -
// [$xxxx]
// [$xx]
// *$xxxx
// *$xxxx+x
// *$xxxx+y
// *$xx
// *$xx+x
// *$xx+y
// *{$xx}
// *{$xx+x}
// *{$xx}+y
// [$xxxx+x]
// [$xxxx+y]
// [$xx+x]
// [$xx+y]
// {$xx}

enum class WatchRefStyle : uint32_t {
	none = 0,
	value = 1,
	value_zp = 2,
	array = 4,
	array_plus_x = 8,
	array_plus_y = 16,
	array_zp = 32,
	array_zp_x = 64,
	array_zp_y = 128,
	array_ref_zp = 256,
	array_ref_zp_x = 512,
	array_ref_zp_y = 1024,
	value_plus_x = 2048,
	value_plus_y = 4096,
	value_zp_plus_x = 8192,
	value_zp_plus_y = 16384,
	eval_zp_plus_x = 32768,
	eval_zp_plus_y = 65536,
	double_zp = 0x20000
};

static const uint32_t AddrMode_WatchRefs[AM_COUNT] = {
	(uint32_t)WatchRefStyle::array_ref_zp_x | (uint32_t)WatchRefStyle::eval_zp_plus_x | (uint32_t)WatchRefStyle::double_zp,	// AM_ZP_REL_X,	// 0 ($12,x)
	(uint32_t)WatchRefStyle::value_zp,		// AM_ZP,			// 1 $12
	(uint32_t)WatchRefStyle::none,			// AM_IMM,			// 2 #$12
	(uint32_t)WatchRefStyle::value | (uint32_t)WatchRefStyle::array, //AM_ABS,			// 3 $1234
	(uint32_t)WatchRefStyle::array_ref_zp | (uint32_t)WatchRefStyle::array_ref_zp_y | (uint32_t)WatchRefStyle::eval_zp_plus_y,	// 4 ($12),y
	(uint32_t)WatchRefStyle::array_zp | (uint32_t)WatchRefStyle::array_zp_x | (uint32_t)WatchRefStyle::value_zp_plus_x | (uint32_t)WatchRefStyle::double_zp, // AM_ZP_X,		// 5 $12,x
	(uint32_t)WatchRefStyle::value | (uint32_t)WatchRefStyle::array | (uint32_t)WatchRefStyle::array_plus_y | (uint32_t)WatchRefStyle::value_plus_y, //AM_ABS_Y,		// 6 $1234,y
	(uint32_t)WatchRefStyle::value | (uint32_t)WatchRefStyle::array | (uint32_t)WatchRefStyle::array_plus_x | (uint32_t)WatchRefStyle::value_plus_x, //AM_ABS_X,		// 7 $1234,x
	(uint32_t)WatchRefStyle::value, // AM_REL,			// 8 ($1234)
	(uint32_t)WatchRefStyle::none, // AM_ACC,			// 9 A
	(uint32_t)WatchRefStyle::none, // AM_NON,			// a
	(uint32_t)WatchRefStyle::none, // AM_BRANCH,		// b $1234
	(uint32_t)WatchRefStyle::array_ref_zp | (uint32_t)WatchRefStyle::array_ref_zp_y, // AM_ZP_REL_Y,	// c
	(uint32_t)WatchRefStyle::array_ref_zp | (uint32_t)WatchRefStyle::value_zp_plus_y, //AM_ZP_Y,		// d
	//AM_COUNT,
};

bool GetWatchRef(CPU6510* cpu, uint16_t addr, int style, char *buf, size_t bufCap) {
	const dismnm* opcodes = a6502_ops;
	uint8_t m = opcodes[cpu->GetByte(addr)].addrMode;
	uint32_t style_mask = AddrMode_WatchRefs[m];
	strovl o(buf, (strl_t)bufCap);
	if ((1<<style) & style_mask) {
		switch ((WatchRefStyle)(1 << style)) {
			case WatchRefStyle::value: // = 1,
				o.append("[$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).append(']').c_str();
				break;
			case WatchRefStyle::value_zp: // = 2,
				o.append("[$").append_num(cpu->GetByte(addr + 1), 2, 16).append(']').c_str();
				break;
			case WatchRefStyle::array: // = 4,
				o.append("*$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).c_str();
				break;
			case WatchRefStyle::array_plus_x: // = 8,
				o.append("*$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).append("+x").c_str();
				break;
			case WatchRefStyle::array_plus_y: // = 16,
				o.append("*$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).append("+y").c_str();
				break;
			case WatchRefStyle::array_zp: // = 32,
				o.append("*$").append_num(cpu->GetByte(addr + 1), 2, 16).c_str();
				break;
			case WatchRefStyle::array_zp_x: // = 64,
				o.append("*$").append_num(cpu->GetByte(addr + 1), 2, 16).append("+x").c_str();
				break;
			case WatchRefStyle::array_zp_y: // = 128,
				o.append("*$").append_num(cpu->GetByte(addr + 1), 2, 16).append("+y").c_str();
				break;
			case WatchRefStyle::array_ref_zp: // = 256,
				o.append("*{$").append_num(cpu->GetByte(addr + 1), 2, 16).append('}').c_str();
				break;
			case WatchRefStyle::array_ref_zp_x: // = 512,
				o.append("*{$").append_num(cpu->GetByte(addr + 1), 2, 16).append("+x}").c_str();
				break;
			case WatchRefStyle::array_ref_zp_y: // = 1024,
				o.append("*{$").append_num(cpu->GetByte(addr + 1), 2, 16).append("}+y").c_str();
				break;
			case WatchRefStyle::value_plus_x: // = 2048,
				o.append("[$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).append("+x]").c_str();
				break;
			case WatchRefStyle::value_plus_y: // = 4096,
				o.append("[$").append_num(cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8),
					4, 16).append("+y]").c_str();
				break;
			case WatchRefStyle::value_zp_plus_x: // = 8192,
				o.append("[$").append_num(cpu->GetByte(addr + 1), 2, 16).append("+x]").c_str();
				break;
			case WatchRefStyle::value_zp_plus_y: // = 16384,
				o.append("[$").append_num(cpu->GetByte(addr + 1), 2, 16).append("+y]").c_str();
				break;
			case WatchRefStyle::eval_zp_plus_x: { // = 32768,
				uint8_t z = cpu->GetByte(addr + 1);
				uint16_t a = cpu->GetByte(z) | (((uint16_t)cpu->GetByte(z + 1)) << 8);
				o.append("*$").append_num(a, 4, 16).append("+x").c_str();
				break;
			}
			case WatchRefStyle::eval_zp_plus_y: { // = 65536,
				uint8_t z = cpu->GetByte(addr + 1);
				uint16_t a = cpu->GetByte(z) | (((uint16_t)cpu->GetByte(z + 1)) << 8);
				o.append("*$").append_num(a, 4, 16).append("+y").c_str();
				break;
			}
			case WatchRefStyle::double_zp: { // = 0x20000,
				uint8_t z = cpu->GetByte(addr + 1);
				uint16_t a = cpu->GetByte(z) | (((uint16_t)cpu->GetByte(z + 1)) << 8);
				o.append("*$").append_num(a, 4, 16).append("").c_str();
				break;
			}
		}
		return true;
	}
	return false;
}

uint16_t InstrRefAddr(CPU6510* cpu, uint16_t addr) {
	const dismnm* opcodes = a6502_ops;
	uint8_t m = opcodes[cpu->GetByte(addr)].addrMode;
	switch (m) {
		case AM_ZP_REL_X:
		{	// 0 ($12:x)
			uint8_t z = cpu->GetByte(addr + 1) + cpu->regs.X;
			return cpu->GetByte(z) + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
		}
		case AM_ZP:
		{	// 1 $12
			uint8_t z = cpu->GetByte(addr + 1);
			return z;
		}
		case AM_ABS:
		{	// 3 $1234
			return cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8);
		}
		case AM_ZP_Y_REL:
		{	// 4 ($12):y
			uint8_t z = cpu->GetByte(addr + 1);
			return cpu->GetByte(z) + cpu->regs.Y + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
		}
		case AM_ZP_X:
		{	// 5 $12:x
			return cpu->GetByte(addr + 1) + cpu->regs.X;
		}
		case AM_ABS_Y:
		{	// 6 $1234:y
			return cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8) + cpu->regs.Y;
		}
		case AM_ABS_X:
		{	// 7 $1234:x
			return cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8) + cpu->regs.X;
		}
		case AM_REL:
		{	// 8 ($1234)
			uint16_t arg = cpu->GetByte(addr + 1) + ((uint16_t)cpu->GetByte((addr + 2)) << 8);
			return cpu->GetByte(arg) + ((uint16_t)cpu->GetByte(((arg + 1) & 0xff) | (arg & 0xff00)) << 8);
		}
		case AM_ACC:
		{	// 9 AS
			break;
		}
		case AM_ZP_REL_Y:
		{	// c ($12:y)
			uint8_t z = cpu->GetByte(addr + 1) + cpu->regs.Y;
			return cpu->GetByte(z) + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
		}
		case AM_ZP_Y:
		{	// d $12:x
			return cpu->GetByte(addr + 1) + cpu->regs.Y;
		}
		case AM_BRANCH:
		{
			return addr + 2 + (int8_t)cpu->GetByte(addr + 1);
		}
	}
	return 0;
}

int InstrRef(CPU6510* cpu, uint16_t pc, char* buf, size_t bufSize)
{
	const dismnm* opcodes = a6502_ops;
	uint8_t m = opcodes[cpu->GetByte(pc)].addrMode;
	strovl str(buf, (strl_t)bufSize);

	switch (m) {
		case AM_ZP_REL_X:
		{	// 0 ($12:x)
			uint8_t z = cpu->GetByte(pc + 1) + cpu->regs.X;
			uint16_t addr = cpu->GetByte(z) + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_ZP:
		{	// 1 $12
			uint8_t z = cpu->GetByte(pc + 1);
			return str.sprintf("(%02x)=%02x", z, cpu->GetByte(z));
		}
		case AM_ABS:
		{	// 3 $1234
			uint16_t addr = cpu->GetByte(pc + 1) + ((uint16_t)cpu->GetByte((pc + 2)) << 8);
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_ZP_Y_REL:
		{	// 4 ($12):y
			uint8_t z = cpu->GetByte(pc + 1);
			uint16_t addr = cpu->GetByte(z) + cpu->regs.Y + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_ZP_X:
		{	// 5 $12:x
			uint8_t z = cpu->GetByte(pc + 1) + cpu->regs.X;
			return str.sprintf("(%02x)=%02x", z, cpu->GetByte(z));
		}
		case AM_ABS_Y:
		{	// 6 $1234:y
			uint16_t addr = cpu->GetByte(pc + 1) + ((uint16_t)cpu->GetByte((pc + 2)) << 8) + cpu->regs.Y;
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_ABS_X:
		{	// 7 $1234:x
			uint16_t addr = cpu->GetByte(pc + 1) + ((uint16_t)cpu->GetByte((pc + 2)) << 8) + cpu->regs.X;
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_REL:
		{	// 8 ($1234)
			uint16_t addr = cpu->GetByte(pc + 1) + ((uint16_t)cpu->GetByte((pc + 2)) << 8);
			uint16_t rel = cpu->GetByte(addr) + ((uint16_t)cpu->GetByte(((addr + 1) & 0xff) | (addr & 0xff00)) << 8);
			return str.sprintf("(%04x)=%04x", addr, rel);
		}
		case AM_ACC:
		{	// 9 AS
			return str.sprintf("A=%02x", cpu->regs.A);
		}
		case AM_ZP_REL_Y:
		{	// c ($12:y)
			uint8_t z = cpu->GetByte(pc + 1) + cpu->regs.Y;
			uint16_t addr = cpu->GetByte(z) + ((uint16_t)cpu->GetByte((z + 1) & 0xff) << 8);
			return str.sprintf("(%04x)=%02x", addr, cpu->GetByte(addr));
		}
		case AM_ZP_Y:
		{	// d $12:x
			uint8_t z = cpu->GetByte(pc + 1) + cpu->regs.Y;
			return str.sprintf("(%02x)=%02x", z, cpu->GetByte(z));
		}
	}
	buf[0] = 0;
	return 0;
}

// disassemble one instruction at addr into the dest string and return number of bytes for instruction
int Disassemble(CPU6510* cpu, uint16_t addr, char* dest, int left, int& argOffs, int& branchTrg, bool showBytes, bool illegals, bool showLabels, bool showDis)
{
	strovl str(dest, left);
	const dismnm* opcodes = a6502_ops;
	int bytes = 1;
	unsigned char op = cpu->GetByte(addr);
	bool not_valid = opcodes[op].mnemonic == mnm_inv || (!illegals && opcodes[op].mnemonic >= mnm_wdc_and_illegal_instructions);

	int arg_size = not_valid ? 0 : opcodes[op].arg_size;;
	int mode = not_valid ? AM_NON : opcodes[op].addrMode;
	bytes += arg_size;

	if (showBytes) {
		for (uint16_t b = 0; b <= (uint16_t)arg_size; b++) {
			str.sprintf_append("%02x ", cpu->GetByte(addr + b));
		}
		str.pad_to(' ', 10);
	}

	if (showDis) {
		if (not_valid) {
			str.sprintf_append("dc.b %02x ", cpu->GetByte(addr));
		} else {
			addr++;
			const char* mnemonic = zsMNM[opcodes[op].mnemonic];
			uint16_t arg;
			const char* label;
			//char label8[256];
			switch (mode) {
				case AM_ABS:		// 3 $1234
				case AM_ABS_Y:		// 6 $1234,y
				case AM_ABS_X:		// 7 $1234,x
				case AM_REL:		// 8 ($1234)
					arg = (uint16_t)cpu->GetByte(addr) | ((uint16_t)cpu->GetByte(addr + 1)) << 8;
					if (op == 0x20 || op == 0x4c) { branchTrg = arg; }
					label = showLabels ? GetSymbol(arg) : nullptr;
					str.append(mnemonic).append(' ');
					argOffs = str.get_len();
					if (label) {
						//size_t newlen = 0;
						//wcstombs_s(&newlen, label8, label, sizeof(label8)-1);
						str.sprintf_append(aAddrModeLblFmt[mode], label, arg);
					} else
						str.sprintf_append(aAddrModeFmt[mode], arg);
					break;

				case AM_BRANCH:		// beq $1234
					arg = addr + 1 + (char)cpu->GetByte(addr);
					branchTrg = arg;
					label = showLabels ? GetSymbol(arg) : nullptr;
					str.append(mnemonic).append(' ');
					argOffs = str.get_len();
					if (label) {
						//size_t newlen = 0;
						//wcstombs_s(&newlen, label8, label, sizeof(label8)-1);
						str.sprintf_append(aAddrModeLblFmt[mode], label, arg);
					} else
						str.sprintf_append(aAddrModeFmt[mode], arg);
					break;

				default:
					str.append(mnemonic).append(' ');
					argOffs = str.get_len();
					str.sprintf_append(aAddrModeFmt[mode],
									   cpu->GetByte(addr), cpu->GetByte(addr + 1));
					break;
			}
		}
	}
	str.c_str();
	return 1 + arg_size;
}

int Assemble(CPU6510* cpu, char* cmd, uint16_t addr)
{
	// skip initial stuff
	while (*cmd && *cmd < 'A') cmd++;

	const char* instr = cmd;
	while (*cmd && *cmd >= 'A') cmd++;

	size_t instr_len = cmd - instr;
	int mnm = -1;

	if (!instr_len) { return 0; }

	while (*cmd && *cmd <= ' ') cmd++;

	for (size_t i = 0; i < (sizeof(zsMNM) / sizeof(zsMNM[0])); i++) {
		if (_strnicmp(zsMNM[i], instr, instr_len) == 0) {
			mnm = (int)i;
			break;
		}
	}

	if (mnm < 0)
		return 0;	// instruction not found

	// get valid address modes
	int addr_modes = 0;
	int addr_mode_mask = 0;
	uint8_t op_code_instr[AM_COUNT] = { (uint8_t)0xff };
	for (int i = 0; i < 256; i++) {
		if (a6502_ops[i].mnemonic == mnm) {
			addr_modes++;
			addr_mode_mask |= 1 << a6502_ops[i].addrMode;
			op_code_instr[a6502_ops[i].addrMode] = (uint8_t)i;
		}
	}

	// get address mode
	bool rel = false;
	bool abs = false;
	bool hex = false;
	bool imm = false;
	bool imp = false;
	bool ix = false;
	bool iy = false;
	int mode = -1;
	switch (*cmd) {
		case 0: imp = true; break;
		case '#': imm = true; ++cmd; break;
		case '$': abs = true; hex = true; ++cmd; break;
		case '(': rel = true; ++cmd; break;
		case 'a':
		case 'A': imp = (cmd[1] <= ' ' || cmd[1] == ';'); break;
	}
	int arg = -1;
	if (!imp) {
		if ((imm || rel) && *cmd == '$') {
			hex = true;
			cmd++;
		}
		if (!hex && !imm && !rel)
			abs = true;
		arg = strtol(cmd, &cmd, hex ? 16 : 10);
		while (*cmd && *cmd <= ' ') cmd++;
		if (*cmd == ',') {
			++cmd;
			while (*cmd && *cmd <= ' ') cmd++;
			if (*cmd == 'x' || *cmd == 'X')
				ix = true;
			else if ((!rel || mnm == mnm_stx || mnm == mnm_ldx) && (*cmd == 'y' || *cmd == 'Y'))
				iy = true;
			else
				return 0;
		}
		if (*cmd == ')') {
			++cmd;
			if (!rel)
				return 0;
		}
		if (*cmd == ',' && rel) {
			++cmd;
			while (*cmd && *cmd <= ' ') cmd++;
			if (*cmd == 'y' || *cmd == 'Y')
				iy = true;
			else
				return 0;
		}
		if (rel)
			mode = (!ix && !iy) ? AM_REL : (ix ? AM_ZP_REL_X : AM_ZP_Y_REL);
		else if (imm)
			mode = AM_IMM;
		else if (abs) {
			if (addr_mode_mask & (1 << AM_BRANCH)) {
				mode = AM_BRANCH;
				arg -= addr + 2;
				if (arg < -128 || arg>127)
					return 0;
			} else if (arg < 0x100 && mnm == mnm_stx && iy)
				mode = AM_ZP_Y;
			else
				mode = (arg < 0x100) ? (ix ? AM_ZP_X : (iy ? AM_ZP_Y : AM_ZP)) :
				(ix ? AM_ABS_X : (iy ? AM_ABS_Y : AM_ABS));
		}
	} else
		mode = (addr_mode_mask & (1 << AM_ACC)) ? AM_ACC : AM_NON;

	if (mode < 0 || !(addr_mode_mask & (1 << mode)))
		return 0;	// invalid mode

	uint8_t op = op_code_instr[mode];
	int bytes = a6502_ops[op].arg_size;
	cpu->SetByte(addr++, op);
	if (bytes) {
		cpu->SetByte(addr++, (uint8_t)arg);
		if (bytes > 1)
			cpu->SetByte(addr++, (uint8_t)(arg >> 8));
	}
	return bytes + 1;
}
