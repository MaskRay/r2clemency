/* radare2 - LGPL - Copyright 2017 - xvilka */

#include <stdio.h>
#include <string.h>
#include <r_types.h>
#include <r_lib.h>
#include <r_asm.h>
#include <r_anal.h>

#include "../include/clemency.h"
#include "../include/disasm.h"

#define CC_SWITCH(...) \
	switch (inst.cc) { \
	case CC_n: r_strbuf_setf (&op->esil, "zf,!" __VA_ARGS__); break; \
	case CC_e: r_strbuf_setf (&op->esil, "zf" __VA_ARGS__); break; \
	case CC_l: r_strbuf_setf (&op->esil, "zf" __VA_ARGS__); break; \
	case CC_le: r_strbuf_setf (&op->esil, "zf,cf,|" __VA_ARGS__); break; \
	case CC_g: r_strbuf_setf (&op->esil, "zf,cf,|,!" __VA_ARGS__); break; \
	case CC_ge: /* I think zf is redundant */ r_strbuf_setf (&op->esil, "zf,cf,!,|" __VA_ARGS__); break; \
	case CC_no: r_strbuf_setf (&op->esil, "of,!" __VA_ARGS__); break; \
	case CC_o: r_strbuf_setf (&op->esil, "of" __VA_ARGS__); break; \
	case CC_ns: r_strbuf_setf (&op->esil, "sf,!" __VA_ARGS__); break; \
	case CC_s: r_strbuf_setf (&op->esil, "sf" __VA_ARGS__); break; \
	case CC_sl: r_strbuf_setf (&op->esil, "of,sf,==,!" __VA_ARGS__); break; \
	case CC_sle: r_strbuf_setf (&op->esil, "zf,of,sf,==,!,|" __VA_ARGS__); break; \
	case CC_sg: r_strbuf_setf (&op->esil, "zf,!,of,sf,==,&" __VA_ARGS__); break; \
	case CC_sge: r_strbuf_setf (&op->esil, "of,sf,==" __VA_ARGS__); break; \
	case CC_invalid: op->type = R_ANAL_OP_TYPE_ILL; break; \
	case CC_always: r_strbuf_setf (&op->esil, "1" __VA_ARGS__); break; \
	}

static int reg_read(RAnalEsil *esil, const char *regname, ut64 *num) {
	RRegItem *reg = r_reg_get (esil->anal->reg, regname, -1);
	if (reg) {
		if (num)
			*num = r_reg_get_value (esil->anal->reg, reg);
		return 1;
	}
	return 0;
}

static int reg_write(RAnalEsil *esil, const char *regname, ut64 num) {
	RRegItem *reg = r_reg_get (esil->anal->reg, regname, -1);
	if (reg) {
		if (num)
			r_reg_set_value (esil->anal->reg, reg,num);
		return 1;
	}
	return 0;

}

static int clemency_op(RAnal *anal, RAnalOp *op, ut64 addr, const ut8 *src, int len) {
	inst_t inst = {.pc = addr};

#define FORMAT(fmt) decode_##fmt (&inst, (const ut16*)src);
#define INS(x,opc) if (inst.opcode == opc) { inst.id = I_##x; break; }
#define INS_1(x,opc,f1,v1) if (inst.opcode == opc && inst.f1 == v1) { inst.id = I_##x; break; }
#define INS_2(x,opc,f1,v1,f2,v2) if (inst.opcode == opc && inst.f1 == v1 && inst.f2 == v2) { inst.id = I_##x; break; }
#define INS_3(x,opc,f1,v1,f2,v2,f3,v3) if (inst.opcode == opc && inst.f1 == v1 && inst.f2 == v2 && inst.f3 == v3) { inst.id = I_##x; break; }
#define INS_4(x,opc,f1,v1,f2,v2,f3,v3,f4,v4) if (inst.opcode == opc && inst.f1 == v1 && inst.f2 == v2 && inst.f3 == v3 && inst.f4 == v4) { inst.id = I_##x; break; }
	bool ok = true;
	do {
#include "../include/opcode-inc.h"
#undef FORMAT
#undef INS
#undef INS_1
#undef INS_2
#undef INS_3
#undef INS_4
		ok = false;
	} while (0);

#define TYPE(inst_, type_) case I_##inst_: op->type = R_ANAL_OP_TYPE_##type_; break
#define TYPE_E(inst_, type_, ...) case I_##inst_: op->type = R_ANAL_OP_TYPE_##type_; r_strbuf_setf (&op->esil, __VA_ARGS__); break
	ZERO_FILL (*op);

	const char *rA = regs[inst.rA], *rB = regs[inst.rB], *rC = regs[inst.rC];
	int imm = (int)inst.imm; // st32 -> int to avoid PRIi32
	op->type = R_ANAL_OP_TYPE_NULL;
	op->jump = op->fail = -1;
	op->ptr = op->val = -1;
	op->addr = addr;

	if (ok) {
		op->size = inst.size;
		switch (inst.id) {
		case I_b:
			op->type = R_ANAL_OP_TYPE_JMP | (inst.cc == CC_always ? 0 : R_ANAL_OP_TYPE_COND);
			op->jump = addr + inst.imm;
			op->fail = addr + op->size;
			CC_SWITCH (",?{,%d,pc,+=,}", inst.imm);
			break;
		case I_br:
			op->type = R_ANAL_OP_TYPE_RCALL | (inst.cc == CC_always ? 0 : R_ANAL_OP_TYPE_COND);
			op->jump = -1;
			op->fail = addr + op->size;
			op->reg = regs[inst.rA];
			CC_SWITCH (",?{,%s,pc,=,}", op->reg);
			break;
		case I_bra:
			op->type = R_ANAL_OP_TYPE_JMP;
			op->jump = inst.imm;
			op->fail = addr + op->size;
			r_strbuf_setf (&op->esil, "%d,pc,=", imm);
			break;
		case I_brr:
			op->type = R_ANAL_OP_TYPE_JMP;
			op->jump = addr + inst.imm;
			op->fail = addr + op->size;
			r_strbuf_setf (&op->esil, "%d,pc,+=", imm);
			break;
		case I_c:
			op->type = R_ANAL_OP_TYPE_CALL | (inst.cc == CC_always ? 0 : R_ANAL_OP_TYPE_COND);
			op->jump = addr + inst.imm;
			op->fail = addr + op->size;
			CC_SWITCH (",?{,3,pc,+,ra,=,%d,pc,+=,}", imm);
			break;
		case I_caa:
			op->type = R_ANAL_OP_TYPE_CALL;
			op->jump = inst.imm;
			op->fail = addr + op->size;
			r_strbuf_setf (&op->esil, "4,pc,+,ra,=,%d,pc,=", imm);
			break;
		case I_car:
			op->type = R_ANAL_OP_TYPE_CALL;
			op->jump = addr + inst.imm;
			op->fail = addr + op->size;
			r_strbuf_setf (&op->esil, "4,pc,+,ra,=,%d,pc,+=", imm);
			break;
		case I_cr:
			op->type = R_ANAL_OP_TYPE_RCALL | (inst.cc == CC_always ? 0 : R_ANAL_OP_TYPE_COND);
			op->jump = -1;
			op->fail = addr + op->size;
			op->reg = regs[inst.rA];
			CC_SWITCH (",?{,3,pc,+,ra,=,%s,pc,=,}", op->reg);
			break;
		case I_lds:
			op->type = R_ANAL_OP_TYPE_LOAD;
			op->refptr = 1;
			r_strbuf_setf (&op->esil, "%s,%d,+,[2],&,0x1ff,%s,=", rB, imm, rA);
			break;
		case I_ldt:
			op->type = R_ANAL_OP_TYPE_LOAD;
			op->refptr = 3;
			r_strbuf_setf (&op->esil, "%s,%d,+,[4],&,0x7ffffff,%s,=", rB, imm, rA);
			break;
		case I_ldw:
			op->type = R_ANAL_OP_TYPE_LOAD;
			op->refptr = 2;
			r_strbuf_setf (&op->esil, "%s,%d,+,[4],&,0x3ffff,%s,=", rB, imm, rA);
			break;
		TYPE_E (ad, ADD, "%s,%s,+,%s,=", rC, rB, rA);
		TYPE_E (adc, ADD, "cf,%s,%s,+,+,%s,=", rC, rB, rA);
		TYPE_E (adci, ADD, "cf,%d,%s,+,+,%s,=", imm, rB, rA);
		TYPE (adcim, ADD);
		TYPE (adcm, ADD);
		TYPE (adf, ADD);
		TYPE (adfm, ADD);
		TYPE_E (adi, ADD, "%d,%s,+,%s,=", imm, rB, rA);
		TYPE (adim, ADD);
		TYPE (adm, ADD);
		TYPE_E (an, AND, "%s,%s,&,%s,=", rC, rB, rA);
		TYPE_E (ani, AND, "%d,%s,&,%s,=", imm, rB, rA);
		TYPE (anm, AND);
		TYPE_E (bf, NOT, "%s,%s,!=", rB, rA);
		TYPE (bfm, NOT);
		TYPE_E (cm, CMP, "%s,%s,==", rB, rA); // XXX
		TYPE (cmf, CMP);
		TYPE (cmfm, CMP);
		TYPE_E (cmi, CMP, "%d,%s,==", imm, rA); // XXX
		TYPE (cmim, CMP);
		TYPE (cmm, CMP);
		TYPE_E (dbrk, TRAP, "0,%d,$", R_ANAL_TRAP_BREAKPOINT);
		TYPE_E (di, MOV, "0x1ff0,4,-1,%s,^,<<,&,0x7ffe00f,fl,&,|", rA);
		TYPE (dmt, MOV);
		TYPE_E (dv, DIV, "%s,%s,/,%s,=", rC, rB, rA);
		TYPE (dvf, DIV);
		TYPE (dvfm, DIV);
		TYPE_E (dvi, DIV, "%d,%s,/,%s,=", imm, rB, rA);
		TYPE (dvim, DIV);
		TYPE_E (dvis, DIV, "%d,%s,/,%s,=", imm, rB, rA); // XXX
		TYPE (dvism, DIV);
		TYPE (dvm, DIV);
		TYPE (dvs, DIV);
		TYPE (dvsm, DIV);
		TYPE_E (ei, MOV, "0x1ff0,4,%s,<<,&,0x7ffe00f,fl,&,|", rA);
		TYPE (fti, MOV);
		TYPE (ftim, MOV);
		TYPE_E (ht, TRAP, "0,%d,$", R_ANAL_TRAP_HALT);
		TYPE (ir, RET);
		TYPE (itf, MOV);
		TYPE (itfm, MOV);
		TYPE_E (md, MOD, "%s,%s,%%,%s,=", rC, rB, rA);
		TYPE (mdf, MOD);
		TYPE (mdfm, MOD);
		TYPE_E (mdi, MOD, "%d,%s,%%,%s,=", imm, rB, rA);
		TYPE (mdim, MOD);
		TYPE_E (mdis, MOD, "%d,%s,%%,%s,=", imm, rB, rA);
		TYPE (mdm, MOD);
		TYPE_E (mds, MOD, "%s,%s,%%,%s,=", rC, rB, rA);
		TYPE (mdsm, MOD);
		TYPE_E (mh, MOV, "0x3ff,%s,&,10,%d,<<,|,%s,=,", rA, imm, rA);
		TYPE_E (ml, MOV, "%d,%s,=", imm, rA);
		TYPE_E (ms, MOV, "%d,%s,=", imm, rA);
		TYPE_E (mu, MUL, "%s,%s,*,%s,=", rC, rB, rA);
		TYPE (muf, MUL);
		TYPE (mufm, MUL);
		TYPE_E (mui, MUL, "%d,%s,*,%s,=", imm, rB, rA);
		TYPE (muim, MUL);
		TYPE_E (muis, MUL, "%d,%s,*,%s,=", imm, rB, rA); // XXX
		TYPE (muism, MUL);
		TYPE (mum, MUL);
		TYPE_E (mus, MUL, "%s,%s,*,%s,=", rC, rB, rA); // XXX
		TYPE (musm, MUL);
		TYPE_E (ng, SUB, "%s,0,-,%s,=", rB, rA);
		TYPE (ngf, SUB);
		TYPE (ngfm, SUB);
		TYPE (ngm, SUB);
		TYPE_E (nt, NOT, "%s,!,%s,=", rB, rA);
		TYPE (ntm, NOT);
		TYPE_E (or, OR, "%s,%s,|,%s,=", rC, rB, rA);
		TYPE_E (ori, OR, "%d,%s,|,%s,=", imm, rB, rA);
		TYPE (orm, OR);
		TYPE_E (re, RET, "ra,pc,=");
		TYPE_E (rf, MOV, "fl,%s,=", rA);
		TYPE (rl, ROL);
		TYPE (rli, ROL);
		TYPE (rlim, ROL);
		TYPE (rlm, ROL);
		TYPE_E (rmp, SWI, "%d,$", I_rmp);
		TYPE_E (rnd, SWI, "%d,$", I_rnd);
		TYPE_E (rndm, SWI, "%d,$", I_rndm);
		TYPE (rr, ROR);
		TYPE (rri, ROR);
		TYPE (rrim, ROR);
		TYPE (rrm, ROR);
		TYPE_E (sa, ROR, "%s,%s,>>>>,%s,=", rC, rB, rA);
		TYPE_E (sai, ROR, "%d,%s,>>>>,%s,=", imm, rB, rA);
		TYPE (saim, SAR);
		TYPE (sam, SAR);
		TYPE_E (sb, SUB, "%s,%s,-,%s,=", rC, rB, rA);
		TYPE_E (sbc, SUB, "cf,%s,%s,-,-,%s,=", rC, rB, rA);
		TYPE_E (sbci, SUB, "cf,%d,%s,-,-,%s,=", imm, rB, rA);
		TYPE (sbcim, SUB);
		TYPE (sbcm, SUB);
		TYPE (sbf, SUB);
		TYPE (sbfm, SUB);
		TYPE_E (sbi, SUB, "%d,%s,-,%s,=", imm, rB, rA);
		TYPE (sbim, SUB);
		TYPE (sbm, SUB);
		TYPE (ses, CPL);
		TYPE (sew, CPL);
		TYPE_E (sf, MOV, "%s,fl,=", rA);
		TYPE_E (sl, SHL, "%s,%s,<<,%s=", rC, rB, rA);
		TYPE_E (sli, SHL, "%d,%s,<<,%s=", imm, rB, rA);
		TYPE (slim, SHL);
		TYPE (slm, SHL);
		TYPE_E (smp, SWI, "%d,$", I_smp);
		TYPE_E (sr, SHR, "%s,%s,>>,%s=", rC, rB, rA);
		TYPE_E (sri, SHR, "%d,%s,>>,%s=", imm, rB, rA);
		TYPE (srim, SHR);
		TYPE (srm, SHR);
		TYPE (sts, STORE);
		TYPE (stt, STORE);
		TYPE (stw, STORE);
		TYPE_E (xr, XOR, "%s,%s,^,%s,=", rC, rB, rA);
		TYPE_E (xri, XOR, "%d,%s,^,%s,=", imm, rB, rA);
		TYPE (xrm, XOR);
		TYPE (zes, CPL);
		TYPE (zew, CPL);
		}
	} else {
		op->size = 1;
	}

	/*
	if (op == NULL) {
		return 1;
	}

	memset (op, 0, sizeof (RAnalOp));
	r_strbuf_init (&op->esil);
	// Wrong - it also has subopcode
	// or run decode function here?
	char *buf = malloc (len);
	
	int i, d = 0;
	for (i = 0; i < len; i += 16) {
		r_mem_copybits_delta (buf, d, src, i, 9);
		d += 9;
	}
	op->size = decode_byte (buf, anal->bitshift, &inst) / 9;
	opcode = inst.mnemonic;
	rA = get_reg_name(inst.rA);
	rB = get_reg_name(inst.rB);
	rC = get_reg_name(inst.rC);
	imm = inst.Immediate_unsigned;
	cond = inst.Condition;

	switch (opcode) {
	case CLCY_AD: // Add
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,+,%s,=",rC,rB,rA);
		break;
	case CLCY_ADC: // Add with curry
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,+,cf,+,%s,=",rC,rB,rA);
		break;
	case CLCY_ADCI: // Add immediate with curry
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64x",+,cf,+,%s,=",rB,imm,rA);
		break;
	// XXX
	case CLCY_ADCIM: // Add immediate multi reg with curry
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64x",+,%s,=",rB,imm,rA);
		break;
	// XXX
	case CLCY_ADCM: // Add multi reg with curry
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,+,%s,=",rC,rB,rA);
		break;
	case CLCY_ADF: // Add floating point
		op->size = 3;
		break;
	case CLCY_ADFM: // Add floating point multi reg
		op->size = 3;
		break;
	case CLCY_ADI: // Add immediate
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64x",+,%s,=",rB,imm,rA);
		break;
	// XXX
	case CLCY_ADIM: // Add immediate multi reg
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		break;
	// XXX
	case CLCY_ADM: // Add multi reg
		op->type = R_ANAL_OP_TYPE_ADD;
		op->size = 3;
		break;
	case CLCY_AN: // And
		op->type = R_ANAL_OP_TYPE_AND;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,&,%s,=,",rC,rB,rA);
		break;
	case CLCY_ANI: // And immediate
		op->type = R_ANAL_OP_TYPE_AND;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64x",&,%s,=,",rB,imm,rA);
		break;
	// XXX
	case CLCY_ANM: // And multi reg
		op->type = R_ANAL_OP_TYPE_AND;
		op->size = 3;
		break;
	case CLCY_B: // Branch conditional
		op->type = R_ANAL_OP_TYPE_CJMP;
		op->size = 3;
		op->jump = imm;
		op->fail = addr + op->size;
		int cond = ((buf[0] & 0xc0) >> 6) | ((buf[1] & 3) << 2);
		switch(cond) {
			case 0: // Not equal / not zero
				r_strbuf_setf(&op->esil, "zf,!,?{,%"PFMT64d",pc,+=,}",imm);
				break;
			case 1: // Equal / Zero
				r_strbuf_setf(&op->esil, "zf,?{,%"PFMT64d",pc,+=,}",imm);
				break;
			case 2: // Less Than
				r_strbuf_setf(&op->esil, "zf,!,cf,&,?{,%"PFMT64d",pc,+=,}",imm);
				break;
			case 3: // Less Than or Equal
				r_strbuf_setf(&op->esil, "zf,cf,|,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 4: // Greater Than
				r_strbuf_setf(&op->esil, "zf,cf,&,!,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 5: // Greater Than or Equal
				r_strbuf_setf(&op->esil, "cf,!,zf,|,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 6: // Not overflow
				r_strbuf_setf(&op->esil, "of,!,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 7: // Overflow
				r_strbuf_setf(&op->esil, "of,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 8: // Not signed
				r_strbuf_setf(&op->esil, "sf,!,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 9: // Signed
				r_strbuf_setf(&op->esil, "sf,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 10: // Signed less than
				r_strbuf_setf(&op->esil, "of,sf,==,!,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 11: // Signed less than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,!,zf,|,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 12: // Signed greater than
				r_strbuf_setf(&op->esil, "zf,!,of,sf,==,&,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			case 13: // Sined Greater Than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,?{,%"PFMT64d",pc,+=,}", imm);
				break;
			default: // Always
				r_strbuf_setf(&op->esil, "%"PFMT64d",pc,+=,", imm);
				break;
		}
		break;
	case CLCY_BF: // Bit flip
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		r_strbuf_setf(&op->esil,"%s,%s,!=,",rB, rA);
		break;
	// XXX
	case CLCY_BFM: // Bit flip multi reg
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		break;
	case CLCY_BR: // Branch register
		op->type = R_ANAL_OP_TYPE_CJMP;
		op->size = 2;
		op->jump = UT64_MAX;
		op->fail = addr + op->size;
		switch(cond) {
			case 0: // Not equal / not zero
				r_strbuf_setf(&op->esil, "zf,!,?{,%s,pc,=,}", rA);
				break;
			case 1: // Equal / Zero
				r_strbuf_setf(&op->esil, "zf,?{,%s,pc,=,}", rA);
				break;
			case 2: // Less Than
				r_strbuf_setf(&op->esil, "zf,!,cf,&,?{,%s,pc,=,}", rA);
				break;
			case 3: // Less Than or Equal
				r_strbuf_setf(&op->esil, "zf,cf,|,?{,%s,pc,=,}", rA);
				break;
			case 4: // Greater Than
				r_strbuf_setf(&op->esil, "zf,cf,&,!,?{,%s,pc,=,}", rA);
				break;
			case 5: // Greater Than or Equal
				r_strbuf_setf(&op->esil, "cf,!,zf,|,?{,%s,pc,=,}", rA);
				break;
			case 6: // Not overflow
				r_strbuf_setf(&op->esil, "of,!,?{,%s,pc,=,}", rA);
				break;
			case 7: // Overflow
				r_strbuf_setf(&op->esil, "of,?{,%s,pc,=,}", rA);
				break;
			case 8: // Not signed
				r_strbuf_setf(&op->esil, "sf,!,?{,%s,pc,=,}", rA);
				break;
			case 9: // Signed
				r_strbuf_setf(&op->esil, "sf,?{,%s,pc,=,}", rA);
				break;
			case 10: // Signed less than
				r_strbuf_setf(&op->esil, "of,sf,==,!,?{,%s,pc,=,}", rA);
				break;
			case 11: // Signed less than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,!,zf,|,?{,%s,pc,=,}", rA);
				break;
			case 12: // Signed greater than
				r_strbuf_setf(&op->esil, "zf,!,of,sf,==,&,?{,%s,pc,=,}", rA);
				break;
			case 13: // Sined Greater Than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,?{,%s,pc,=,}", rA);
				break;
			default: // Always
				r_strbuf_setf(&op->esil, "%s,pc,=,", rA);
				break;
		}
		break;
	case CLCY_BRA: // Branch absolute
		op->type = R_ANAL_OP_TYPE_JMP;
		op->size = 4;
		op->jump = imm;
		op->fail = addr + op->size;
		r_strbuf_setf(&op->esil, "%"PFMT64x",pc,=,",imm);
		break;
	case CLCY_BRR: // Branch relative
		op->type = R_ANAL_OP_TYPE_JMP;
		op->size = 4;
		op->jump = imm;
		op->fail = addr + op->size;
		r_strbuf_setf(&op->esil, "%"PFMT64x",pc,+=,",imm);
		break;
	case CLCY_C: // Call conditional
		op->type = R_ANAL_OP_TYPE_CCALL;
		op->size = 3;
		op->jump = imm;
		op->fail = addr + op->size;
		switch(cond) {
			case 0: // Not equal / not zero
				r_strbuf_setf(&op->esil, "zf,!,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}",rA, imm);
				break;
			case 1: // Equal / Zero
				r_strbuf_setf(&op->esil, "zf,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}",rA, imm);
				break;
			case 2: // Less Than
				r_strbuf_setf(&op->esil, "zf,!,cf,&,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}",rA, imm);
				break;
			case 3: // Less Than or Equal
				r_strbuf_setf(&op->esil, "zf,cf,|,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 4: // Greater Than
				r_strbuf_setf(&op->esil, "zf,cf,&,!,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 5: // Greater Than or Equal
				r_strbuf_setf(&op->esil, "cf,!,zf,|,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 6: // Not overflow
				r_strbuf_setf(&op->esil, "of,!,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 7: // Overflow
				r_strbuf_setf(&op->esil, "of,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 8: // Not signed
				r_strbuf_setf(&op->esil, "sf,!,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 9: // Signed
				r_strbuf_setf(&op->esil, "sf,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 10: // Signed less than
				r_strbuf_setf(&op->esil, "of,sf,==,!,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 11: // Signed less than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,!,zf,|,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 12: // Signed greater than
				r_strbuf_setf(&op->esil, "zf,!,of,sf,==,&,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			case 13: // Sined Greater Than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,?{,pc,3,+,%s,=,%"PFMT64d",pc,+=,}", rA, imm);
				break;
			default: // Always
				r_strbuf_setf(&op->esil, "pc,3,+,%s,=,%"PFMT64d",pc,+=,", rA, imm);
				break;
		}
		break;
	// XXX
	case CLCY_CM: // Compare
		op->type = R_ANAL_OP_TYPE_CMP;
		op->size = 2;
		r_strbuf_setf (&op->esil, "%s,%s,==,", rA, rB);
		break;
	case CLCY_CMF: // Compare Floating Point
		op->size = 2;
		break;
	case CLCY_CMFM: // Compare floating point multi reg
		op->size = 2;
		break;
	case CLCY_CMI: // Compare immediate
		op->type = R_ANAL_OP_TYPE_CMP;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64d",==,", rA, imm);
		break;
	// XXX
	case CLCY_CMIM: // Compare immediate multi reg
		op->type = R_ANAL_OP_TYPE_CMP;
		op->size = 3;
		break;
	// XXX
	case CLCY_CMM: // Compare multi reg
		op->type = R_ANAL_OP_TYPE_CMP;
		op->size = 2;
		break;
	// XX
	case CLCY_CR: // Call register conditional
		op->type = R_ANAL_OP_TYPE_CCALL;
		op->size = 2;
		switch(cond) {
			case 0: // Not equal / not zero
				r_strbuf_setf(&op->esil, "zf,!,?{,pc,2,+,%s,=,%s,pc,=,}",rA, rA);
				break;
			case 1: // Equal / Zero
				r_strbuf_setf(&op->esil, "zf,?{,pc,2,+,%s,=,%s,pc,=,}",rA, rA);
				break;
			case 2: // Less Than
				r_strbuf_setf(&op->esil, "zf,!,cf,&,?{,pc,2,+,%s,=,%s,pc,=,}",rA, rA);
				break;
			case 3: // Less Than or Equal
				r_strbuf_setf(&op->esil, "zf,cf,|,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 4: // Greater Than
				r_strbuf_setf(&op->esil, "zf,cf,&,!,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 5: // Greater Than or Equal
				r_strbuf_setf(&op->esil, "cf,!,zf,|,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 6: // Not overflow
				r_strbuf_setf(&op->esil, "of,!,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 7: // Overflow
				r_strbuf_setf(&op->esil, "of,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 8: // Not signed
				r_strbuf_setf(&op->esil, "sf,!,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 9: // Signed
				r_strbuf_setf(&op->esil, "sf,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 10: // Signed less than
				r_strbuf_setf(&op->esil, "of,sf,==,!,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 11: // Signed less than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,!,zf,|,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 12: // Signed greater than
				r_strbuf_setf(&op->esil, "zf,!,of,sf,==,&,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			case 13: // Sined Greater Than or Equal
				r_strbuf_setf(&op->esil, "of,sf,==,?{,pc,2,+,%s,=,%s,pc,=,}", rA, rA);
				break;
			default: // Always
				r_strbuf_setf(&op->esil, "pc,2,+,%s,=,%s,pc,=,", rA, rA);
				break;
		}

		break;
	case CLCY_DI: // Disable interrupts
		op->size = 2;
		break;
	case CLCY_DBRK: // Debug break
		op->type = R_ANAL_OP_TYPE_TRAP;
		op->size = 2;
		break;
	// XXX
	case CLCY_DMT: // Direct memory transfer
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	case CLCY_DV: // Divide
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,/,%s,=,",rC,rB,rA);
		break;
	case CLCY_DVF: // Divide floating point
		op->size = 3;
		break;
	case CLCY_DVFM: // Divide floating point multi reg
		op->size = 3;
		break;
	case CLCY_DVI: // Divide immediate
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,/,%s,=,",imm,rB,rA);
		break;
	case CLCY_DVIM: // Divide immediate multi reg
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		break;
	case CLCY_DVIS: // Divide immediate signed
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,/,%s,=,",imm,rB,rA);
		break;
	case CLCY_DVISM: // Divide immediate signed multi reg
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		break;
	case CLCY_DVM: // Divide multi reg
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		break;
	case CLCY_DVS: // Divide signed
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,/,%s,=,",rC,rB,rA);
		break;
	case CLCY_DVSM: // Divide signed multi reg
		op->type = R_ANAL_OP_TYPE_DIV;
		op->size = 3;
		break;
	case CLCY_EI: // Enable interrupts
		op->size = 2;
		break;
	case CLCY_FTI: // Float to integer
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	case CLCY_FTIM: // Float to integer multi reg
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	case CLCY_HT: // Halt
		op->type = R_ANAL_OP_TYPE_TRAP;
		op->size = 2;
		break;
	case CLCY_IR: // Interrupt return
		op->size = 2;
		break;
	case CLCY_ITF: // Integer to float
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	case CLCY_ITFM: // Integer to float multi reg
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	// Allow only aligned to 32bits access
	case CLCY_LDS: // Load single
		op->type = R_ANAL_OP_TYPE_LOAD;
		op->size = 6;
		op->refptr = 6;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64d",+,[4],0x1f,&,%s,=,", rB, imm, rA);
		break;
	// Allow only aligned to 32bits access
	case CLCY_LDT: // Load tri
		op->type = R_ANAL_OP_TYPE_LOAD;
		op->size = 6;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64d",+,[4],0xffffff,&,%s,=,", rB, imm, rA);
		break;
	// Allow only aligned to 32bits access
	case CLCY_LDW: // Load word
		op->type = R_ANAL_OP_TYPE_LOAD;
		op->size = 6;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64d",+,[4],0x3ffff,&,%s,=,", rB, imm, rA);
		break;
	case CLCY_MD: // Modulus
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,%c,%s,=,",rC,rB,'%',rA);
		break;
	case CLCY_MDF: // Modulus floating point
		op->size = 3;
		break;
	case CLCY_MDFM: // Modulus floating point multi reg
		op->size = 3;
		break;
	case CLCY_MDI: // Modulus immediate
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,%c,%s,=,",imm,rB,'%',rA);
		break;
	case CLCY_MDIM: // Modulus immediate multi reg
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		break;
	case CLCY_MDIS: // Modulus immediate signed
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,%c,%s,=,",imm,rB,'%',rA);
		break;
	case CLCY_MDISM: // Modulus immediate signed multi reg
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		break;
	case CLCY_MDM: // Modulus multi reg
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		break;
	case CLCY_MDS: // Modulus signed
		op->type = R_ANAL_OP_TYPE_MOD;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,%c,%s,=,",rC,rB,'%',rA);
		break;
	case CLCY_MDSM: // Modulus signed multi reg
		op->type = R_ANAL_OP_TYPE_REG;
		op->size = 3;
		break;
	case CLCY_MH: // Move high
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",10,<<,%s,0x3ff,&,|,%s,=,", imm, rA, rA);
		break;
	case CLCY_ML: // Move low
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,=,", imm, rA);
		break;
	case CLCY_MS: // Move low signed
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,=,", imm, rA);
		break;
	case CLCY_MU: // Multiply
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,*,%s,=,",rC,rB,rA);
		break;
	case CLCY_MUF: // Multiply floating point
		op->size = 3;
		break;
	case CLCY_MUFM: // Multiply floating point multi reg
		op->size = 3;
		break;
	case CLCY_MUI: // Multiply immediate
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,*,%s,=,",imm,rB,rA);
		break;
	case CLCY_MUIM: // Multiply immediate multi reg
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		break;
	case CLCY_MUIS: // Multiply immediate signed
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64d",*,%s,=,",rB, imm, rA);
		break;
	case CLCY_MUISM: // Multiply immediate signed multi reg
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		break;
	case CLCY_MUM: // Multiply multi reg
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		break;
	case CLCY_MUS: // Multiply signed
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,*,%s,=,",rC,rB,rA);
		break;
	case CLCY_MUSM: // Multiply signed multi reg
		op->type = R_ANAL_OP_TYPE_MUL;
		op->size = 3;
		break;
	case CLCY_NG: // Negate (nougat)
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,0,-,%s,=,", rB, rA);
		break;
	case CLCY_NGF: // Negate floating point
		op->size = 3;
		break;
	case CLCY_NGFM: // Negate floating point multi reg
		op->size = 3;
		break;
	case CLCY_NGM: // Negate multi reg
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		break;
	case CLCY_NT: // Nooooooooooooooooot
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,!,%s,=,", rB, rA);
		break;
	case CLCY_NTM: // Not multi reg
		op->type = R_ANAL_OP_TYPE_NOT;
		op->size = 3;
		break;
	case CLCY_OR: // Or
		op->type = R_ANAL_OP_TYPE_OR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,|,%s,=,",rC,rB,rA);
		break;
	case CLCY_ORI: // Ori
		op->type = R_ANAL_OP_TYPE_OR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%"PFMT64x",|,%s,=,",rB,imm,rA);
		break;
	case CLCY_ORM: // Or multi reg
		op->type = R_ANAL_OP_TYPE_OR;
		op->size = 3;
		break;
	case CLCY_RE: // Return
		op->type = R_ANAL_OP_TYPE_RET;
		op->size = 2;
		break;
	case CLCY_RF: // Read flags
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 2;
		break;
	// XXX - be careful with rotation - may be use not embeded commands?
	case CLCY_RL: // Rotate left
		op->type = R_ANAL_OP_TYPE_ROL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,<<<,%s,=",rC,rB,rA);
		break;
	case CLCY_RLI: // Rotate left immediate
		op->type = R_ANAL_OP_TYPE_ROL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,<<<,%s,=",imm, rB, rA);
		break;
	case CLCY_RLIM: // Rotate left immediate multi reg
		op->type = R_ANAL_OP_TYPE_ROL;
		op->size = 3;
		break;
	case CLCY_RLM: // Rotate left multi reg
		op->type = R_ANAL_OP_TYPE_ROL;
		op->size = 3;
		break;
	case CLCY_RMP: // Read memory protection
		op->size = 3;
		break;
	case CLCY_RND: // Random
		op->size = 3;
		break;
	case CLCY_RNDM: // Random multi reg
		op->size = 3;
		break;
	case CLCY_RR: // Rotate right
		op->type = R_ANAL_OP_TYPE_ROR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,>>>,%s,=",rC,rB,rA);
		break;
	case CLCY_RRI: // Rotate right immediate
		op->type = R_ANAL_OP_TYPE_ROR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,>>>,%s,=",imm, rB, rA);
		break;
	case CLCY_RRIM: // Rotate right immediate multi reg
		op->type = R_ANAL_OP_TYPE_ROR;
		op->size = 3;
		break;
	case CLCY_RRM: // Rotate right multi reg
		op->type = R_ANAL_OP_TYPE_ROR;
		op->size = 3;
		break;
	case CLCY_SA: // Shift arithmetic right
		op->type = R_ANAL_OP_TYPE_SAR;
		op->size = 3;
		break;
	case CLCY_SAI: // Shift arithmetic right immediate
		op->type = R_ANAL_OP_TYPE_SAR;
		op->size = 3;
		break;
	case CLCY_SAIM: // Shift arithmetic right immediate multi reg
		op->type = R_ANAL_OP_TYPE_SAR;
		op->size = 3;
		break;
	case CLCY_SAM: // Shift arithmetic right multi reg
		op->type = R_ANAL_OP_TYPE_SAR;
		op->size = 3;
		break;
	case CLCY_SB: // Subtract
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,-,%s,=",rC,rB,rA);
		break;
	case CLCY_SBC: // Subtract with carry
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,-,%s,=",rC,rB,rA);
		break;
	case CLCY_SBCI: // Subtract immediate with carry
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,-,%s,=",imm,rB,rA);
		break;
	case CLCY_SBCIM: // Subtract immediate multi reg with carry
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		break;
	case CLCY_SBCM: // Subtract multi reg with carry
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		break;
	case CLCY_SBF: // Subtract floating point
		op->size = 3;
		break;
	case CLCY_SBFM: // Subtract floating point multi reg
		op->size = 3;
		break;
	case CLCY_SBI: // Subtract immediate
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,-,%s,=",imm,rB,rA);
		break;
	case CLCY_SBIM: // Subtract immediate multi reg;
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		break;
	case CLCY_SBM: // Subtract multi reg
		op->type = R_ANAL_OP_TYPE_SUB;
		op->size = 3;
		break;
	case CLCY_SES: // Sign extend single
		op->size = 3;
		break;
	case CLCY_SEW: // Sign extend word
		op->size = 3;
		break;
	case CLCY_SF: // Set flags
		op->type = R_ANAL_OP_TYPE_MOV;
		op->size = 3;
		break;
	case CLCY_SL: // Shift left
		op->type = R_ANAL_OP_TYPE_SHL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,<<,%s,=",rC,rB,rA);
		break;
	case CLCY_SLI: // Shift left immediate
		op->type = R_ANAL_OP_TYPE_SHL;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,<<,%s,=,",imm,rB,rA);
		break;
	case CLCY_SLIM: // Shift left immediate multi reg
		op->type = R_ANAL_OP_TYPE_SHL;
		op->size = 3;
		break;
	case CLCY_SLM: // Shift left multi reg
		op->type = R_ANAL_OP_TYPE_SHL;
		op->size = 3;
		break;
	case CLCY_SMP: // Set memory protection
		op->size = 3;
		break;
	case CLCY_SR: // Shift right
		op->type = R_ANAL_OP_TYPE_SHR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%s,%s,>>,%s,=",rC,rB,rA);
		break;
	case CLCY_SRI: // Shift right immediate
		op->type = R_ANAL_OP_TYPE_SHR;
		op->size = 3;
		r_strbuf_setf (&op->esil, "%"PFMT64d",%s,>>,%s,=,",imm,rB,rA);
		break;
	case CLCY_SRIM: // Shift right immediate multi reg
		op->type = R_ANAL_OP_TYPE_SHR;
		op->size = 3;
		break;
	case CLCY_SRM: // Shift right multi reg
		op->type = R_ANAL_OP_TYPE_SHR;
		op->size = 3;
		break;
	// It aligns the storage to 32bits
	case CLCY_STS: // Store single
		op->type = R_ANAL_OP_TYPE_STORE;
		op->size = 6;
		r_strbuf_setf (&op->esil, ",%s,0x1f,&,%"PFMT64d",%s,+,=[4],", rA, imm, rB);
		break;
	// It aligns the storage to 32bits
	case CLCY_STT: // Store tri
		op->type = R_ANAL_OP_TYPE_STORE;
		op->size = 6;
		r_strbuf_setf (&op->esil, ",%s,0xfffffff,&,%"PFMT64d",%s,+,=[4],", rA, imm, rB);
		break;
	// It aligns the storage to 32bits
	case CLCY_STW: // Store word
		op->type = R_ANAL_OP_TYPE_STORE;
		op->size = 6;
		r_strbuf_setf (&op->esil, ",%s,0x3ff,&,%"PFMT64d",%s,+,=[4],", rA, imm, rB);
		break;
	case CLCY_WT: // Wait
		op->size = 2;
		break;
	case CLCY_XRM: // Xor multi reg
		op->type = R_ANAL_OP_TYPE_XOR;
		op->size = 3;
		break;
	case CLCY_ZES: // Zero extend single
		op->size = 3;
		break;
	case CLCY_ZEW: // Zero extend word
		op->size = 3;
		break;
	default:
		op->type = R_ANAL_OP_TYPE_NOP;
		op->size = 1;
		r_strbuf_setf (&op->esil, "nop");
		break;
	}
	anal->bitshift = 0; //(op->size * 9 + anal->bitshift) % 8;
	free (buf);
	*/
	return op->size;
}

static void *  internalMemory = NULL;
static int indicememoria = 0;
static ut32 vtmp = 0;
static ut32 idxInputText = 0;
static char texto[] ="packers_and_vms_and_xors_oh_my\n";

static int esil_clemency_intr (RAnalEsil *esil, int intr) {
	ut64 valor1;
	if (!esil)
		return false;
	if (intr==0) {
		reg_read(esil,"r_00",&valor1);
		eprintf("%c\n",(ut32)valor1);
	} else if (intr==0x4) {
		eprintf("Leido %c\n",texto[idxInputText]);
		reg_write(esil,"r_00",(ut64)((char) texto[idxInputText++]));
	}
	else if (intr==0x11) {
		ut64 basedata=0;
		reg_read(esil,"r_00",&valor1);
		reg_read(esil,"r_data",&basedata);
		int  v1=indicememoria;
		indicememoria+= valor1;

		reg_write(esil,"r_00",(ut64) basedata+v1);
	}
	else
		eprintf ("INTERRUPT 0x%02x \n", intr);
	return true;
}

static int set_reg_profile(RAnal *anal) {
	// esil does not support 27-bit registers, thus padding them to 32-bit.
	const char *p = \
		"#r0  return value\n"
		"=PC  pc\n"
		"=SP  st\n"
		"=BP  r28\n"
		"=A0  r0\n"
		"=A1  r1\n"
		"=A2  r2\n"
		"=A3  r3\n"
		"=A4  r4\n"
		"=A5  r5\n"
		"=A6  r6\n"
		"=A7  r7\n"
		"=A8  r8\n"
		"gpr	r0	.32	0	  0\n"
		"gpr	r1	.32	4	  0\n"
		"gpr	r2	.32	8	  0\n"
		"gpr	r3	.32	12	0\n"
		"gpr	r4	.32	16	0\n"
		"gpr	r5	.32	20	0\n"
		"gpr	r6	.32	24	0\n"
		"gpr	r7	.32	28	0\n"
		"gpr	r8	.32	32	0\n"
		"gpr	r9	.32	36	0\n"
		"gpr	r10	.32	40	0\n"
		"gpr	r11	.32	44	0\n"
		"gpr	r12	.32	48	0\n"
		"gpr	r13	.32	52	0\n"
		"gpr	r14	.32	56	0\n"
		"gpr	r15	.32	60	0\n"
		"gpr	r16	.32	64	0\n"
		"gpr	r17	.32	68	0\n"
		"gpr	r18	.32	72	0\n"
		"gpr	r19	.32	76	0\n"
		"gpr	r20	.32	80	0\n"
		"gpr	r21	.32	84	0\n"
		"gpr	r22	.32	88	0\n"
		"gpr	r23	.32	92	0\n"
		"gpr	r24	.32	96	0\n"
		"gpr	r25	.32	100	0\n"
		"gpr	r26	.32	104	0\n"
		"gpr	r27	.32	108	0\n"
		"gpr	r28	.32	112	0\n"
		"gpr	st	.32	116	0\n"
		"gpr	ra	.32	120	0\n"
		"gpr	pc	.32	124	0\n"
		"flg	fl	.32	128	0 zcos\n"
		"flg	zf	.1	128.0	0\n"
		"flg	cf	.1	128.1	0\n"
		"flg	of	.1	128.2	0\n"
		"flg	sf	.1	128.3	0\n";

	return r_reg_set_profile_string (anal->reg, p);
}
static int esil_clemency_init (RAnalEsil *esil) {
	if (!esil) return false;
	return true;
}

static int esil_clemency_fini (RAnalEsil *esil) {
	return true;
}

static RAnalPlugin r_anal_plugin_clemency = {
	.name = "clcy",
	.desc = "cLEMENCy analysis",
	.license = "LGPL3",
	.arch = "clcy",
	.bits = 27,
	.esil_init = esil_clemency_init,
	.esil_fini = esil_clemency_fini,
	.esil_intr = esil_clemency_intr,
	.esil = true,
	.op = &clemency_op,
	.set_reg_profile = set_reg_profile,
};

RLibStruct radare_plugin = {
	.type = R_LIB_TYPE_ANAL,
	.data = &r_anal_plugin_clemency,
	.version = R2_VERSION,
};
