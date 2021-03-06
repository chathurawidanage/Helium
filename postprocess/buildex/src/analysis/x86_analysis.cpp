#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <string>

#include "analysis\x86_analysis.h"
#include "utility\fileparser.h" /* disasm strings*/
#include "utility\defines.h"
#include "utility\print_helper.h" /* printing opnd etc.*/
#include "analysis\staticinfo.h"


using namespace std;


/* EFLAGS bit positions */

enum eflag_bits {

	Reserved_31,
	Reserved_30,
	Reserved_29,
	Reserved_28,
	Reserved_27,
	Reserved_26,
	Reserved_25,
	Reserved_24,
	Reserved_23,
	Reserved_22,
	ID_Flag,
	Virtual_Interrupt_Pending,
	Virtual_Interrupt_Flag,
	Alignment_Check,
	Virtual_Mode,
	Resume_Flag,
	Reserved_15,
	Nested_Task,
	IO_Privilege_Level,
	Overflow_Flag,
	Direction_Flag,
	Interrupt_Enable_Flag,
	Trap_Flag,
	Sign_Flag,
	Zero_Flag,
	Reserved_5,
	Auxiliary_Carry_Flag,
	Reserved_3,
	Parity_Flag,
	Reserved_1,
	Carry_Flag

};

enum lahf_bits {

	/*Sign_lahf,
	Zero_lahf,
	Reserved_3,
	Auxiliary_lahf,
	Reserved_5,
	Parity_lahf,
	Reserved_7,
	Carry_lahf*/

	Carry_lahf,
	Reserved_7_lahf,
	Parity_lahf,
	Reserved_5_lahf,
	Auxiliary_lahf,
	Reserved_3_lahf,
	Zero_lahf,
	Sign_lahf,
	Overflow_lahf
	

};


#define SET_FLAG(flag,member) flag |= (1 << member)

#define assign_value(start,opnd)  \
	opnd->value = (start) * MAX_SIZE_OF_REG - opnd->width; \
	 break

#define abcd_reg(v,start,opnd) \
	case DR_REG_R##v##X:  \
	case DR_REG_E##v##X:  \
	case DR_REG_##v##X:   \
	case DR_REG_##v##H:   \
	case DR_REG_##v##L:   \
						  \
	 opnd->value = (start) * MAX_SIZE_OF_REG - opnd->width; \
	 if( value == DR_REG_##v##H ){						  \
		opnd->value = (start) * MAX_SIZE_OF_REG - 2;      \
	 }													  \
	 break

#define sbsd_reg(v,start,opnd) \
	case DR_REG_R##v##:		   \
	case DR_REG_E##v##:		   \
	case DR_REG_##v##:         \
	case DR_REG_##v##L:        \
	assign_value(start,opnd)   

#define x64_reg(v,start,opnd) \
	case DR_REG_R##v##:       \
	case DR_REG_R##v##D:      \
	case DR_REG_R##v##W:      \
	case DR_REG_R##v##L:      \
	assign_value(start,opnd)  

#define mmx_reg(v,start,end)  \
	case DR_REG_MM##v##:      \
	case DR_REG_XMM##v##:	  \
	case DR_REG_YMM##v##:     \
	assign_value(start,opnd)

#define new_mmx_reg(v, start, end) \
	case DR_REG_XMM##v##:	  \
	case DR_REG_YMM##v##:     \
	assign_value(start,opnd)

#define fp_reg(v,start,end)  \
	case DR_REG_##v##:        \
	assign_value(start,opnd)

#define seg_reg(v,start,end)  \
	case DR_SEG_##v##:        \
	assign_value(start,opnd)

#define if_bounds(d,s)  if( (cinstr->num_dsts == d ) && (cinstr->num_srcs == s ) )
#define elseif_bounds(d,s) else if( (cinstr->num_dsts == d ) && (cinstr->num_srcs == s ) )
#define else_bounds else{ unhandled = true; } break

/* function prototypes */
static void populate_rinstr(rinstr_t * rinstr, operand_t dst, int num_srcs, operand_t src1, operand_t src2, int operation, bool sign);
bool check_eflag_bit(eflag_bits flag_type, uint32_t reg_val);
bool check_lahf_bit(lahf_bits flag_type, uint32_t reg_val);
static void assert_opnds(int opcode, int needed_src, int needed_dst, int actual_src, int actual_dst);

/************************************ CISC to RISC ****************************************************************/

cinstr_t * create_new_cinstr(const cinstr_t  &instr){
	cinstr_t * new_instr = new cinstr_t(instr);
	for (int i = 0; i < instr.num_dsts; i++){
		if (instr.dsts[i].addr != NULL){
			new_instr->dsts[i].addr = new operand_t[4];
			for (int j = 0; j < 4; j++){
				new_instr->dsts[i].addr[j] = instr.dsts[i].addr[j];
			}
		}
	}
	for (int i = 0; i < instr.num_srcs; i++){
		if (instr.srcs[i].addr != NULL){
			new_instr->srcs[i].addr = new operand_t[4];
			for (int j = 0; j < 4; j++){
				new_instr->srcs[i].addr[j] = instr.srcs[i].addr[j];
			}
		}
	}
	return new_instr;
}

/* this gets true dependancies + instructions affecting eflags 
Function - cinstr_to_rinstrs_eflags

Parameters - refer to cinstr_to_rinstrs function
Return - refer to cinstr_to_rinstrs function

This function is similar to cinstr_to_rinstrs in all ways except in addition, it canonicalizes instructions which affect eflags 
but does not write to any destination permanently.

*/
rinstr_t * cinstr_to_rinstrs_eflags(cinstr_t * cinstr, int &amount, std::string disasm, uint32_t line){

	//get already handled instructions which pose true dependencies - this will decide whether an instruction is not handled at all
	rinstr_t * rinstr = cinstr_to_rinstrs(cinstr, amount, disasm, line);

	bool unhandled = false;
	//check for non-true dependancies; but ins which affect eflags

	if (rinstr == NULL){
		switch (cinstr->opcode){
		case OP_cmp:
			if_bounds(0, 2){
				rinstr = new rinstr_t[1];
				amount = 1;
				operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1 };
				reg_to_mem_range(&virtual_reg);
				rinstr[0] = { op_sub, virtual_reg, 2, { cinstr->srcs[0], cinstr->srcs[1] }, true };
			}
			else_bounds;
		case OP_test:
			if_bounds(0, 2){
				rinstr = new rinstr_t[1];
				amount = 1;
				operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1 };
				reg_to_mem_range(&virtual_reg);
				rinstr[0] = { op_and, virtual_reg, 2, { cinstr->srcs[0], cinstr->srcs[1] }, true };
			}
			else_bounds;
		}

		ASSERT_MSG((!unhandled), ("ERROR: opcode %s(%d) with %d dests and %d srcs (app_pc - %d) not handled in canonicalization\n", dr_operation_to_string(cinstr->opcode).c_str(), cinstr->opcode,
			cinstr->num_dsts, cinstr->num_srcs, cinstr->pc));
	}

	return rinstr;

}

/* this is a pure function without side effects for cinstr - check to make sure */

/*
Function - cinstr_to_rinstrs

Parameters
 cinstr - pointer to a pre-populated complex x86 instruction
 amount - pass by reference variable, which will contain how many reduced set instructions were need to canonicalize the given x86 instruction
 disasm - string of the disassembly of 'cinstr'. Can be used for debuggin purposes 
 line	- the line in which this instruction can be found in the instruction trace (after filtering)

Return
 rinstr_t * - an array of reduced instructions

 Please fill out new instructions using the guidelines given in the README file.

*/
rinstr_t * cinstr_to_rinstrs(cinstr_t * cinstr, int &amount, std::string disasm, uint32_t line){

	int operation;

	DEBUG_PRINT(("entering canonicalization - app_pc %u\n", cinstr->pc), 4);

	rinstr_t * rinstr;
	rinstr = NULL;
	amount = 0;

	bool unhandled = false;


	switch (cinstr->opcode){

		/************************* vector instructions ****************************/
	case OP_movss:
		//Kevin - to test
		if_bounds(1,1){
			// move low 32 bits from src to dst. Upper bits unchanged if dst is register
			// bits 127-32 zeroed if source is memory
			operand_t dst = cinstr -> dsts[0];
			operand_t src = cinstr -> srcs[0];
			// get low 32 bits of dst (or 4 bytes)
			operand_t dst_low = {dst.type, 4, dst.value + dst.width - 4, NULL };
			operand_t src_low = {src.type, 4, src.value + src.width - 4, NULL };
			if ( dst.type == MEM_STACK_TYPE || dst.type == MEM_HEAP_TYPE ){
				rinstr = new rinstr_t[2];
				rinstr[0] = { op_assign, dst_low, 1, { src_low }, true };
				//get upper bits
				operand_t dst_high = {dst.type, 12, dst.value + dst.width - 16, NULL};
				// move zeros into upper bits
				operand_t immediate = { IMM_INT_TYPE, 12, 0 , NULL};
				rinstr[1] = { op_assign, dst_high, 1, { immediate }, true };
			}
			else {
				rinstr = new rinstr_t[1];
				rinstr[0] = { op_assign, dst_low, 1, { src_low }, true };
			}
		}
		else_bounds;

	case OP_pmuldq:
		// Kevin - to test
		if_bounds(2,1){
			// If destination larger than 128 bit, should keep upper bits unchanged
			// dsts[0][63:0] <- srcs[0][31:0] * srcs[1][31:0]
			// dsts[0][127:64] <- srcs[0][95:64] * srcs[1][95:64]

			amount = 2;
			rinstr = new rinstr_t[amount];
			// create virtual reg/mem accessing 32 bit input ranges tmp 64 bit reg and 2 tmp 32 bit reg
			operand_t input1 = cinstr -> srcs[0];
			operand_t input2 = cinstr -> srcs[1];
			operand_t output = cinstr -> dsts[0];
			// offset starting point by correct number of BYTES (divide by 8 to change from bits to bytes)
			operand_t input32_1 = { input1.type, 4, input1.value + input1.width - 4, NULL };
			operand_t input32_2 = { input2.type, 4, input2.value + input2.width - 4, NULL };
			operand_t input96_1 = { input1.type, 4, input1.value + input1.width - 12, NULL };
			operand_t input96_2 = { input2.type, 4, input2.value + input2.width - 12, NULL };
			// create virtual reg/mem accessing 8 byte output ranges
			operand_t output_low = { output.type, 8, output.value + output.width - 8, NULL };
			operand_t output_high = { output.type, 8, output.value + output.width - 16, NULL };

			// out_low <- input32_1 * input32_2
			rinstr[0] = { op_mul, output_low, 2, { input32_1, input32_2 }, true };
			// out_high <- input96_1 * input96_2
			rinstr[1] = { op_mul, output_high, 2, { input96_1, input96_2 }, true };
		}
		else_bounds;

		/******************************************integer instructions****************************************************************/

	case OP_push_imm:
	case OP_push:
		// [esp - 4] (dst[1]) <- src[0]
		if_bounds(2, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[1], 1, { cinstr->srcs[0] }, false };
		}
		else_bounds;

	case OP_pop:
		// dst[0] <- [esp] (src[1])
		if_bounds(2, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[1] }, false };
		}
		else_bounds;

	case OP_mov_st:
	case OP_mov_ld:
	case OP_mov_imm:
	case OP_movzx:
	case OP_movsx:
	case OP_movq:
	case OP_movd:
	case OP_movapd:
	case OP_movdqa:
	case OP_mov_seg:
	case OP_movaps: // Kevin - to test

	case OP_vmovss: // check!!
	case OP_vmovsd: //check !

	case OP_vcvtsi2ss: // check!!
	case OP_vcvtsi2sd: // check !!
	case OP_vcvttss2si: // check !! 
	case OP_vcvttsd2si: // cehck !!

	case OP_cvttsd2si: // check !!
	
		// dst[0] <- src[0]
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[0] }, false };
		}
		elseif_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[1] }, false };
		}
		else_bounds;

	case OP_vmulss: // check !!
	case OP_vmulsd:
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_mul, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[1] }, false };
		}
		else_bounds;

	case OP_vaddsd:
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_add, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[1] }, false };
		}
		else_bounds;


	case OP_imul:
		// 1st flavour -> 1 dst * 2 src
		//dst[0] <- src[0] * src[1]
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_mul, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[1] }, true };
		}
		elseif_bounds(2, 2){
			// edx [dst0] : eax [dst1] <- eax [src1] * [src0] 
			rinstr = new rinstr_t[3];
			amount = 3;
			// create an operand for the virtual register
			operand_t virtual_reg = { REG_TYPE, 2 * cinstr->srcs[1].width, DR_REG_VIRTUAL_1 , NULL};
			reg_to_mem_range(&virtual_reg);

			//virtual <= eax * src0
			rinstr[0] = { op_mul, virtual_reg, 2, { cinstr->srcs[1], cinstr->srcs[0] }, true };

			//edx <= split_h(virtual)
			rinstr[1] = { op_split_h, cinstr->dsts[0], 1, { virtual_reg }, true };

			//eax <= split_l(virtual)
			rinstr[2] = { op_split_l, cinstr->dsts[1], 1, { virtual_reg }, true };
		}
		else_bounds;


	case OP_mul:

		if_bounds(2, 2){
			// edx [dst0] : eax [dst1] <- eax [src1] * [src0] 
			rinstr = new rinstr_t[3];
			amount = 3;
			// create an operand for the virtual register
			operand_t virtual_reg = { REG_TYPE, 2 * cinstr->srcs[1].width, DR_REG_VIRTUAL_1 , NULL};
			reg_to_mem_range(&virtual_reg);

			//virtual <= eax * src0
			rinstr[0] = { op_mul, virtual_reg, 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };

			//edx <= split_h(virtual)
			rinstr[1] = { op_split_h, cinstr->dsts[0], 1, { virtual_reg }, false };

			//eax <= split_l(virtual)
			rinstr[2] = { op_split_l, cinstr->dsts[1], 1, { virtual_reg }, false };

		}
		else_bounds;

	case OP_idiv:
		//dst - edx / dx , eax / ax , src - src[0], edx / dx , eax / ax
		if_bounds(2, 3){
			rinstr = new rinstr_t[3];
			amount = 3;
			/* create an operand for the virtual register */
			operand_t virtual_reg = { REG_TYPE, 2 * cinstr->srcs[1].width, DR_REG_VIRTUAL_1 , NULL};
			reg_to_mem_range(&virtual_reg);

			//virtual <- edx:eax
			rinstr[0] = { op_concat, virtual_reg, 2, { cinstr->srcs[1], cinstr->srcs[2] }, false };

			//edx <- virtual % src[0]
			rinstr[1] = { op_mod, cinstr->dsts[0], 2, { virtual_reg, cinstr->srcs[0] }, true };

			//eax <- virtual / src[0]
			rinstr[2] = { op_div, cinstr->dsts[1], 2, { virtual_reg, cinstr->srcs[0] }, true };
		}
		else_bounds;

	case OP_cdq:
		// need to change
		// edx <- eax
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_signex, cinstr->dsts[0], 1, { cinstr->srcs[0] }, true };

		}
		else_bounds;

	case OP_cwde:
		// eax <- ax
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_signex, cinstr->dsts[0], 1, { cinstr->srcs[0] }, true };

		}
		else_bounds;

	case OP_xchg:
		//exchange the two registers
		if_bounds(2, 2){
			rinstr = new rinstr_t[3];
			amount = 3;
			operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1 , NULL};
			reg_to_mem_range(&virtual_reg);

			ASSERT_MSG(((cinstr->dsts[0].value == cinstr->srcs[0].value) && (cinstr->dsts[1].value == cinstr->srcs[1].value)), ("op_fxch the dsts and srcs should match\n"));

			//virtual <- src[0]
			rinstr[0] = { op_assign, virtual_reg, 1, { cinstr->srcs[0] }, false };
			//dst[0] <- src[1]
			rinstr[1] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[1] }, false };
			//dst[1] <- virtual 
			rinstr[2] = { op_assign, cinstr->dsts[1], 1, { virtual_reg }, false };
		}
		else_bounds;

	case OP_xorps:// KEVIN - test
	case OP_xor:
	case OP_sub:
	case OP_pxor:
	case OP_psubd:


		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;

			uint32_t operation;
			switch (cinstr->opcode){
			case OP_xorps: // KEVIN -test
			case OP_xor:
			case OP_pxor:
				operation = op_xor; break;
			case OP_sub:
			case OP_psubd:
				operation = op_sub; break;

			}

			operand_t first = cinstr->srcs[0];
			operand_t second = cinstr->srcs[1];
			if (first.type == second.type && first.value == second.value){
				operand_t zero = { IMM_INT_TYPE, cinstr->srcs[0].width, 0 , NULL};
				rinstr[0] = { op_assign, cinstr->dsts[0], 1, { zero }, false };
			}
			else{
				rinstr[0] = { operation, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };
			}

		}
		else_bounds;

	case OP_add:
	case OP_and:
	case OP_or:
	case OP_andpd:

		// dst[0] <- src[1] (op) src[0]
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			uint32_t operation;
			switch (cinstr->opcode){

			case OP_add:
				operation = op_add; break;
			case OP_and:
			case OP_andpd:
				operation = op_and; break;
			case OP_or: operation = op_or; break;
			}
			rinstr[0] = { operation, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };  /* changed for SUB (src1, src0) from the reverse: please verify */
		}
		else_bounds;

	case OP_neg:
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_sub, cinstr->dsts[0], 1, { cinstr->srcs[0] }, false };
		}
		else_bounds;

	case OP_dec:
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			operand_t immediate = { IMM_INT_TYPE, cinstr->srcs[0].width, 1 , NULL};
			rinstr[0] = { op_sub, cinstr->dsts[0], 2, { cinstr->srcs[0], immediate }, true };
		}
		else_bounds;

	case OP_inc:
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			operand_t immediate = { IMM_INT_TYPE, cinstr->srcs[0].width, 1 , NULL};
			rinstr[0] = { op_add, cinstr->dsts[0], 2, { cinstr->srcs[0], immediate }, true };
		}
		else_bounds;

	case OP_sar:

		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_rsh, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, true };
		}
		else_bounds;

	case OP_shr:
	case OP_psrlq:

		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_rsh, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };
		}
		else_bounds;

	case OP_shl:
	case OP_psllq:

		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_lsh, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };
		}
		else_bounds;

	case OP_not:

		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_not, cinstr->dsts[0], 1, { cinstr->srcs[0] }, false };
		}
		else_bounds;

	case OP_lea:

		//[base, index, scale, disp]
		if_bounds(1, 4){
			if (cinstr->srcs[0].value == 0 && cinstr->srcs[0].type == REG_TYPE){
				cinstr->srcs[0].type = IMM_INT_TYPE;
				cinstr->srcs[0].width = 4;
				cinstr->srcs[0].value = 0;
			}

			if (cinstr->srcs[2].value == 0){
				rinstr = new rinstr_t[1];
				amount = 1;
				//dst <= base(src0) + disp(src3)
				rinstr[0] = { op_add, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[3] }, true };

			}
			else{
				rinstr = new rinstr_t[3];
				amount = 3;
				operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1 , NULL};
				reg_to_mem_range(&virtual_reg);

				//virtual <= scale(src2) * index(src1)
				rinstr[0] = { op_mul, virtual_reg, 2, { cinstr->srcs[2], cinstr->srcs[1] }, true };
				//virtual <= virtual + base(src0)
				rinstr[1] = { op_add, virtual_reg, 2, { virtual_reg, cinstr->srcs[0] }, true };
				//dst <= virtual + disp
				rinstr[2] = { op_add, cinstr->dsts[0], 2, { virtual_reg, cinstr->srcs[3] }, true };

			}

		}
		else_bounds;

	case OP_sbb:

		if_bounds(1, 2){
			bool cf = check_lahf_bit(Carry_lahf, cinstr->eflags);
			if (cf){
				rinstr = new rinstr_t[2];
				amount = 2;
			}
			else{
				rinstr = new rinstr_t[1];
				amount = 1;
			}
			//dsts[0] <- srcs[1] - srcs[0]
			if ((cinstr->srcs[0].type == cinstr->srcs[1].type) && (cinstr->srcs[0].value == cinstr->srcs[1].value)){
				operand_t immediate_0 = { IMM_INT_TYPE, cinstr->srcs[1].width, 0 , NULL};
				rinstr[0] = { op_assign, cinstr->dsts[0], 1, { immediate_0 }, true };
			}
			else{
				rinstr[0] = { op_sub, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, true };  /* changed for SUB (src1, src0) from the reverse: please verify */
			}
			if (cf){ //substract an immediate 1
				//dsts[0] <- dsts[0] - 1
				operand_t immediate_1 = { IMM_INT_TYPE, cinstr->srcs[1].width, 1 , NULL};
				rinstr[1] = { op_sub, cinstr->dsts[0], 2, { cinstr->dsts[0], immediate_1 }, true };
			}
		}
		else_bounds;

	case OP_adc:

		if_bounds(1, 2){

			bool cf = check_lahf_bit(Carry_lahf, cinstr->eflags);
			if (cf){
				rinstr = new rinstr_t[2];
				amount = 2;
			}
			else{
				rinstr = new rinstr_t[1];
				amount = 1;
			}

			//dsts[0] <- srcs[1] + srcs[0]
			rinstr[0] = { op_add, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, true };
			//dsts[0] <- dsts[0] + 1
			if (cf){
				operand_t immediate_1 = { IMM_INT_TYPE, cinstr->srcs[1].width, 1, NULL };
				rinstr[1] = { op_add, cinstr->dsts[0], 2, { cinstr->dsts[0], immediate_1}, true };
			}
		}
		else_bounds;

	case OP_cmovle:
	case OP_cmovnle:
	case OP_cmovl:
	case OP_cmovnl:
	case OP_cmovz:
	case OP_cmovns:
	case OP_cmovnz:
		if_bounds(1, 2){

			bool zf = check_lahf_bit(Zero_lahf, cinstr->eflags);
			bool sf = check_lahf_bit(Sign_lahf, cinstr->eflags);
			bool of = check_lahf_bit(Overflow_lahf, cinstr->eflags);


			bool check = false;
			if (cinstr->opcode == OP_cmovl) check = sf != of;
			else if (cinstr->opcode == OP_cmovle) check = zf || (sf != of) ;
			else if (cinstr->opcode == OP_cmovnle) check = !zf || (sf == of);
			else if (cinstr->opcode == OP_cmovnl) check = sf == of;
			else if (cinstr->opcode == OP_cmovz) check = zf;
			else if (cinstr->opcode == OP_cmovnz) check = !zf;
			else if (cinstr->opcode == OP_cmovns) check = !sf;

			if (check){
				rinstr = new rinstr_t[1];
				amount = 1;
				rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[0] }, true };
			}
			else{
				amount = 0;
			}

		}
		else_bounds;

	case OP_setz:
	case OP_sets:
	case OP_setns:
	case OP_setnz:
	case OP_setb:

		if_bounds(1, 0){
			rinstr = new rinstr_t[1];
			amount = 1;
			bool flag;
			switch (cinstr->opcode){
			case OP_setz: flag = check_lahf_bit(Zero_lahf, cinstr->eflags); break;
			case OP_setnz: flag = check_lahf_bit(Zero_lahf, cinstr->eflags); break;
			case OP_sets: flag = check_lahf_bit(Sign_lahf, cinstr->eflags); break;
			case OP_setns: flag = !check_lahf_bit(Sign_lahf, cinstr->eflags); break;
			case OP_setb: flag = check_lahf_bit(Carry_lahf, cinstr->eflags); break;
			}
			if (flag){
				operand_t immediate = { IMM_INT_TYPE, cinstr->dsts[0].width, 1, NULL};
				rinstr[0] = { op_assign, cinstr->dsts[0], 1, { immediate }, true };
			}
			else{
				operand_t immediate = { IMM_INT_TYPE, cinstr->dsts[0].width, 0, NULL};
				rinstr[0] = { op_assign, cinstr->dsts[0], 1, { immediate }, true };
			}
		}
		else_bounds;

	case OP_xadd:

		if_bounds(2, 2){
			rinstr = new rinstr_t[3];
			operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1, NULL};
			reg_to_mem_range(&virtual_reg);
			rinstr[0] = { op_add, virtual_reg, 2, { cinstr->srcs[0], cinstr->srcs[1] }, true };
			rinstr[1] = { op_assign, cinstr->srcs[1], 1, { cinstr->srcs[0] }, true };
			rinstr[2] = { op_assign, cinstr->srcs[0], 1, { virtual_reg }, true };
			amount = 3;

		}
		else_bounds;

		/***************************************************floating point instructions*********************************************************/

		/* floating point instructions */

	case OP_fld: //Push m32fp onto the FPU register stack.
	case OP_fld1: //Push +1.0 onto the FPU register stack
	case OP_fild: //Push m32int onto the FPU register stack.
	case OP_fldz: //Push +0.0 onto the FPU register stack.
		// dst[0] <- src[0]
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[0] }, false };

		}
		else_bounds;


	case OP_fst:
	case OP_fstp:  //Copy ST(0) to m32fp and pop register stack.
	case OP_fistp:  //Store ST(0) in m32int and pop register stack.
		if_bounds(1, 1){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[0] }, false };

		}
		else_bounds;

	case OP_fmul: //Multiply ST(0) by m32fp and store result in ST(0).
	case OP_fmulp:  //Multiply ST(i) by ST(0), store result in ST(i), and pop the register stack.
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_mul, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[1] }, true };

		}
		else_bounds;

		//Exchange the contents of ST(0) and ST(i).
	case OP_fxch:
		//exchange the two registers
		if_bounds(2, 2){
			rinstr = new rinstr_t[3];
			amount = 3;
			operand_t virtual_reg = { REG_TYPE, cinstr->srcs[0].width, DR_REG_VIRTUAL_1, NULL};
			reg_to_mem_range(&virtual_reg);
			ASSERT_MSG(((cinstr->dsts[0].value == cinstr->srcs[0].value) && (cinstr->dsts[1].value == cinstr->srcs[1].value)), ("op_fxch the dsts and srcs should match\n"));
			//virtual <- src[0]
			rinstr[0] = { op_assign, virtual_reg, 1, { cinstr->srcs[0] }, false };
			//dst[0] <- src[1]
			rinstr[1] = { op_assign, cinstr->dsts[0], 1, { cinstr->srcs[1] }, false };
			//dst[1] <- virtual 
			rinstr[2] = { op_assign, cinstr->dsts[1], 1, { virtual_reg }, false };
		}
		else_bounds;

	case OP_faddp:  //Add ST(0) to ST(i), store result in ST(i), and pop the register stack
	case OP_fadd:   //Add m32fp to ST(0) and store result in ST(0).
	case OP_fsubp:  //Subtract ST(0) from ST(1), store result in ST(1), and pop register stack.
	case OP_fsub:   //Subtract m32fp from ST(0) and store result in ST(0).
	case OP_fdivp:  //Divide ST(1) by ST(0), store result in ST(1), and pop the register stack.
	case OP_fdiv:   //Divide ST(0) by m32fp and store result in ST(0).
		// dst[0] <- src[1] (op) src[0]
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			uint32_t operation;
			switch (cinstr->opcode){
			case OP_faddp:
			case OP_fadd:
				operation = op_add; break;
			case OP_fsubp:
			case OP_fsub:
				operation = op_sub; break;
			case OP_fdiv:
			case OP_fdivp:
				operation = op_div; break;
			}
			rinstr[0] = { operation, cinstr->dsts[0], 2, { cinstr->srcs[1], cinstr->srcs[0] }, false };  /* changed for SUB (src1, src0) from the reverse: please verify */


		}
		else_bounds;

	case OP_fsubr:
		if_bounds(1, 2){
			rinstr = new rinstr_t[1];
			amount = 1;
			rinstr[0] = { op_sub, cinstr->dsts[0], 2, { cinstr->srcs[0], cinstr->srcs[1] }, false };
		}
		else_bounds;

		/******************************************************control flow instructions**************************************************************************************************/

	case OP_btr:

	case OP_cmpxchg:
	case OP_rep_stos:
	case OP_cld:
	case OP_jbe:

	case OP_fcom:
	case OP_fcomp:

		/* above change */

	case OP_jmp:
	case OP_jmp_short:
	case OP_jnl:
	case OP_jnl_short:
	case OP_jl:
	case OP_jnle:
	case OP_jnle_short:
	case OP_jnz:
	case OP_jnz_short:
	case OP_jz:
	case OP_jnb_short:
	case OP_jb_short:
	case OP_jz_short:
	case OP_jl_short:
	case OP_jns_short:
	case OP_js_short:
	case OP_jnbe_short:
	case OP_jle_short:
	case OP_jle:
	case OP_jbe_short:
	case OP_jns:
	case OP_jb:
	case OP_jnb:
	case OP_js:
	case OP_jmp_ind:
	case OP_jnbe:

	case OP_cmp:
	case OP_test:

	case OP_call:
	case OP_ret:
	case OP_call_ind:

		/* need to check these as they change esp and ebp ; for now just disregard */
	case OP_enter:
	case OP_leave:

		/* floating point control word stores and loads */
	case OP_fldcw:
	case OP_fnstcw:
	case OP_fnstsw:
	case OP_stmxcsr:
	case OP_fwait:

	case OP_nop_modrm:



	case OP_nop:


		break;

	default:
		unhandled = true;


	}

	ASSERT_MSG((!unhandled), ("ERROR: opcode %s(%d) with %d dests and %d srcs (app_pc - %d) not handled in canonicalization\n", dr_operation_to_string(cinstr->opcode).c_str(), cinstr->opcode,
		cinstr->num_dsts, cinstr->num_srcs, cinstr->pc));

	if (rinstr == NULL){
		DEBUG_PRINT(("opcode skipped\n"), 4);
	}
	else{
		DEBUG_PRINT(("opcode reduced\n"), 4);
	}

	for (int i = 0; i < amount; i++){
		rinstr[i].is_floating = is_floating_point_ins(cinstr->opcode);
	}

	return rinstr;
}

void cinstr_convert_reg(cinstr_t * instr){

	for (int i = 0; i < instr->num_srcs; i++){
		if ((instr->srcs[i].type == REG_TYPE) && (instr->srcs[i].value > DR_REG_ST7)) instr->srcs[i].value += 8;
		reg_to_mem_range(&instr->srcs[i]);
	}

	for (int i = 0; i < instr->num_dsts; i++){
		if (instr->dsts[i].type == REG_TYPE && (instr->dsts[i].value > DR_REG_ST7)) instr->dsts[i].value += 8;
		reg_to_mem_range(&instr->dsts[i]);
	}

}

/*********************************** mapping registers to memory **************************************************/

void reg_to_mem_range(operand_t * opnd){

	if (opnd->type == REG_TYPE){

		uint64 value = opnd->value;

#if DEBUG_LEVEL >= 3
		printf("%d\n", opnd->value);
		printf("before %s ", regname_to_string(opnd->value).c_str());
#endif


		switch (value){

			//abcd regs
			abcd_reg(A, 1, opnd);
			abcd_reg(B, 2, opnd);
			abcd_reg(C, 3, opnd);
			abcd_reg(D, 4, opnd);

			//sp,bp,si,di regs
			sbsd_reg(SP, 5, opnd);
			sbsd_reg(BP, 6, opnd);
			sbsd_reg(SI, 7, opnd);
			sbsd_reg(DI, 8, opnd);

			//x64 regs
			x64_reg(8, 9, opnd);
			x64_reg(9, 10, opnd);
			x64_reg(10, 11, opnd);
			x64_reg(11, 12, opnd);
			x64_reg(12, 13, opnd);
			x64_reg(13, 14, opnd);
			x64_reg(14, 15, opnd);
			x64_reg(15, 16, opnd);

			//mmx regs
			mmx_reg(0, 17, opnd);
			mmx_reg(1, 18, opnd);
			mmx_reg(2, 19, opnd);
			mmx_reg(3, 20, opnd);
			mmx_reg(4, 21, opnd);
			mmx_reg(5, 22, opnd);
			mmx_reg(6, 23, opnd);
			mmx_reg(7, 24, opnd);

			//new_mmx_regs
			new_mmx_reg(8, 25, opnd);
			new_mmx_reg(9, 26, opnd);
			new_mmx_reg(10, 27, opnd);
			new_mmx_reg(11, 28, opnd);
			new_mmx_reg(12, 29, opnd);
			new_mmx_reg(13, 30, opnd);
			new_mmx_reg(14, 31, opnd);
			new_mmx_reg(15, 32, opnd);

			//floating point regs
			fp_reg(ST0, 33, opnd);
			fp_reg(ST1, 34, opnd);
			fp_reg(ST2, 35, opnd);
			fp_reg(ST3, 36, opnd);
			fp_reg(ST4, 37, opnd);
			fp_reg(ST5, 38, opnd);
			fp_reg(ST6, 39, opnd);
			fp_reg(ST7, 40, opnd);

			//8 registers kept for the floating point stack extension
			fp_reg(ST8, 41, opnd);
			fp_reg(ST9, 42, opnd);
			fp_reg(ST10, 43, opnd);
			fp_reg(ST11, 44, opnd);
			fp_reg(ST12, 45, opnd);
			fp_reg(ST13, 46, opnd);
			fp_reg(ST14, 47, opnd);
			fp_reg(ST15, 48, opnd);

			//segments
			seg_reg(ES, 49, opnd);
			seg_reg(CS, 50, opnd);
			seg_reg(SS, 51, opnd);
			seg_reg(DS, 52, opnd);
			seg_reg(FS, 53, opnd);
			seg_reg(GS, 54, opnd);

			//virtual regs
			fp_reg(VIRTUAL_1, 55, opnd);
			fp_reg(VIRTUAL_2, 56, opnd);

			//null register
		case DR_REG_NULL:
			DEBUG_PRINT(("warning NULL reg found - check if OP_lea\n"), 4);
			break;

		default:
			ASSERT_MSG(false, ("ERROR: %d register not translated", value));

		}

#if DEBUG_LEVEL >= 3
		printf("after %s\n", opnd_to_string(opnd).c_str());
#endif

	}
	else if (((opnd->type == MEM_HEAP_TYPE) || (opnd->type == MEM_STACK_TYPE)) && (opnd->width != 0)){
		if (opnd->value < MAX_SIZE_OF_REG * 57){
			DEBUG_PRINT(("WARNING: memory and register space overlap\n"), 2);
			DEBUG_PRINT(("mem value - %d, min allowed - %d\n", opnd->value, (uint32_t)(MAX_SIZE_OF_REG * 57)),2);
		}
		//ASSERT_MSG((opnd->value > MAX_SIZE_OF_REG * 57), ("ERROR: memory and register space overlap\n"));
	}



}

int mem_range_to_reg(operand_t * opnd){

	if (opnd->type == REG_TYPE){

		uint64 range = opnd->value / MAX_SIZE_OF_REG;
		range++;

		switch (range){

		case 1: return DR_REG_RAX;
		case 2: return DR_REG_RBX;
		case 3: return DR_REG_RCX;
		case 4: return DR_REG_RDX;
		case 5: return DR_REG_RSP;
		case 6: return DR_REG_RBP;
		case 7: return DR_REG_RSI;
		case 8: return DR_REG_RDI;

		case 9: return DR_REG_R8;
		case 10: return DR_REG_R9;
		case 11: return DR_REG_R10;
		case 12: return DR_REG_R11;
		case 13: return DR_REG_R12;
		case 14: return DR_REG_R13;
		case 15: return DR_REG_R14;
		case 16: return DR_REG_R15;

		case 17: return DR_REG_YMM0;
		case 18: return DR_REG_YMM1;
		case 19: return DR_REG_YMM2;
		case 20: return DR_REG_YMM3;
		case 21: return DR_REG_YMM4;
		case 22: return DR_REG_YMM5;
		case 23: return DR_REG_YMM6;
		case 24: return DR_REG_YMM7;
		case 25: return DR_REG_YMM8;
		case 26: return DR_REG_YMM9;
		case 27: return DR_REG_YMM10;
		case 28: return DR_REG_YMM11;
		case 29: return DR_REG_YMM12;
		case 30: return DR_REG_YMM13;
		case 31: return DR_REG_YMM14;
		case 32: return DR_REG_YMM15;

		case 33: return DR_REG_ST0;
		case 34: return DR_REG_ST1;
		case 35: return DR_REG_ST2;
		case 36: return DR_REG_ST3;
		case 37: return DR_REG_ST4;
		case 38: return DR_REG_ST5;
		case 39: return DR_REG_ST6;
		case 40: return DR_REG_ST7;

		case 41: return DR_REG_ST8;
		case 42: return DR_REG_ST9;
		case 43: return DR_REG_ST10;
		case 44: return DR_REG_ST11;
		case 45: return DR_REG_ST12;
		case 46: return DR_REG_ST13;
		case 47: return DR_REG_ST14;
		case 48: return DR_REG_ST15;

		case 49: return DR_SEG_ES;
		case 50: return DR_SEG_CS;
		case 51: return DR_SEG_SS;
		case 52: return DR_SEG_DS;
		case 53: return DR_SEG_FS;
		case 54: return DR_SEG_GS;

		case 55: return DR_REG_VIRTUAL_1;
		case 56: return DR_REG_VIRTUAL_2;

		default: return -1;


		}

	}
	else{
		return -1;
	}


}

void update_regs_to_mem_range(vec_cinstr &instrs){

	DEBUG_PRINT(("converting reg to mem\n"), 2);

	for (int i = 0; i < instrs.size(); i++){
		cinstr_t * instr = instrs[i].first;
		//print_cinstr(instrs[i].first);
		for (int i = 0; i < instr->num_srcs; i++){
			if ((instr->srcs[i].type == REG_TYPE) && (instr->srcs[i].value > DR_REG_ST7)) instr->srcs[i].value += 8;
			reg_to_mem_range(&instr->srcs[i]);
			if (instr->srcs[i].addr != NULL){
				for (int j = 0; j < 4; j++){
					if (instr->srcs[i].addr[j].type == REG_TYPE && instr->srcs[i].addr[j].value == 0){
						continue;
					}
					reg_to_mem_range(&instr->srcs[i].addr[j]);
				}
			}
		}

		for (int i = 0; i < instr->num_dsts; i++){
			if (instr->dsts[i].type == REG_TYPE && (instr->dsts[i].value > DR_REG_ST7)) instr->dsts[i].value += 8;
			reg_to_mem_range(&instr->dsts[i]);
			if (instr->dsts[i].addr != NULL){
				for (int j = 0; j < 4; j++){
					if (instr->dsts[i].addr[j].type == REG_TYPE && instr->dsts[i].addr[j].value == 0){
						continue;
					}
					reg_to_mem_range(&instr->dsts[i].addr[j]);
				}
			}
		}
	}

}

/************************************* handling floating point instructions ******************************************/

#define FP_PUSH	1
#define FP_POP	2

int32_t tos = DR_REG_ST8;

bool is_floating_point_ins(uint32_t opcode){
	switch (opcode){

	case OP_fld: //Push m32fp onto the FPU register stack.
	case OP_fld1: //Push +1.0 onto the FPU register stack
	case OP_fild: //Push m32int onto the FPU register stack.
	case OP_fldz: //Push +0.0 onto the FPU register stack.
	case OP_fst: //Copy ST(0) to m32fp.
	case OP_fstp:  //Copy ST(0) to m32fp and pop register stack.
	case OP_fistp:  //Store ST(0) in m32int and pop register stack.
	case OP_fmul: //Multiply ST(0) by m32fp and store result in ST(0).
	case OP_fmulp:  //Multiply ST(i) by ST(0), store result in ST(i), and pop the register stack.
	case OP_fxch:
	case OP_faddp:  //Add ST(0) to ST(i), store result in ST(i), and pop the register stack
	case OP_fadd:   //Add m32fp to ST(0) and store result in ST(0).
	case OP_fsubp:  //Subtract ST(0) from ST(1), store result in ST(1), and pop register stack.
	case OP_fsub:   //Subtract m32fp from ST(0) and store result in ST(0).
	case OP_fdivp:  //Divide ST(1) by ST(0), store result in ST(1), and pop the register stack.
	case OP_fdiv:   //Divide ST(0) by m32fp and store result in ST(0).
	case OP_fcomp:
		return true;
	default:
		return false;
	}

}

bool is_floating_point_reg(operand_t * opnd){

	int reg = mem_range_to_reg(opnd);
	return (
		(opnd->type == REG_TYPE) &&
		(reg >= DR_REG_ST0) &&
		(reg <= DR_REG_ST7)
		);
}

void get_floating_point_reg(operand_t  * opnd, string disasm, uint32_t line){

	int32_t reg = mem_range_to_reg(opnd);
	int32_t offset = reg - DR_REG_ST0;
	int32_t ret = tos - offset;
	//cout << line << " floating point reg : " << regname_to_string(ret)  << " " << disasm << endl;
	//cout << "tos_f: " << tos << endl;
	ASSERT_MSG((ret <= (int32_t)DR_REG_ST15) && (ret >= (int32_t)DR_REG_ST0), ("ERROR: floating point stack under/overflow"));
	opnd->value = (uint32_t)ret;
	reg_to_mem_range(opnd);
}

void update_tos(uint32_t type, string disasm, uint32_t line, uint32_t direction){
	
	if (direction == BACKWARD_ANALYSIS){
		if (type == FP_PUSH){
			ASSERT_MSG(tos >= DR_REG_ST0, ("ERROR: floating point stack overflow\n"));
			tos--;
		}
		else if (type == FP_POP){
			ASSERT_MSG(tos < DR_REG_ST15, ("ERROR: floating point stack underflow\n"));
			tos++;
		}
	}
	else if (direction == FORWARD_ANALYSIS){
		if (type == FP_POP){
			ASSERT_MSG(tos >= DR_REG_ST0, ("ERROR: floating point stack overflow\n"));
			tos--;
		}
		else if (type == FP_PUSH){
			ASSERT_MSG(tos < DR_REG_ST15, ("ERROR: floating point stack underflow\n"));
			tos++;
		}
	}

	//cout << line << " " << disasm <<  " - tos : " << tos - DR_REG_ST8 << endl;
	

}

void update_fp_reg(cinstr_t * cinstr, string disasm, uint32_t line){

	for (int i = 0; i < cinstr->num_dsts; i++){
		if (is_floating_point_reg(&cinstr->dsts[i])){
			get_floating_point_reg(&cinstr->dsts[i], disasm, line);
		}
	}
	for (int i = 0; i < cinstr->num_srcs; i++){
		if (is_floating_point_reg(&cinstr->srcs[i])){
			get_floating_point_reg(&cinstr->srcs[i], disasm, line);
		}
	}

}

void update_fp_dest(cinstr_t * cinstr, string disasm, uint32_t line){
	for (int i = 0; i < cinstr->num_dsts; i++){
		if (is_floating_point_reg(&cinstr->dsts[i])){
			get_floating_point_reg(&cinstr->dsts[i], disasm, line);
		}
	}
}

void update_fp_src(cinstr_t * cinstr, string disasm, uint32_t line){
	for (int i = 0; i < cinstr->num_srcs; i++){
		if (is_floating_point_reg(&cinstr->srcs[i])){
			get_floating_point_reg(&cinstr->srcs[i], disasm, line);
		}
	}
}

void update_floating_point_regs(vec_cinstr &instrs, uint32_t direction, vector<Static_Info *> static_info, vector<uint32_t> pc){

	DEBUG_PRINT(("updating floating point regs\n"), 2);

	tos = DR_REG_ST8;


	for (int i = 0; i < instrs.size(); i++){

		cinstr_t * cinstr = instrs[i].first;
		bool unhandled = false;
		string disasm = get_disasm_string(static_info, cinstr->pc);
		//cout << vector[0] << endl;
	
		uint32_t line = i + 1;

		/* reset for start of the function */
		for (int j = 0; j < pc.size(); j++){
			if (cinstr->pc == pc[j]){
				tos = DR_REG_ST8;
				break;
			}
		}

		int amount;
		//cinstr_to_rinstrs(cinstr, amount, "", 5);

		if (!is_floating_point_ins(cinstr->opcode)){
			update_fp_reg(cinstr, disasm, i + 1);
		}
		else{
			switch (cinstr->opcode){
			case OP_fld: //Push m32fp onto the FPU register stack.
			case OP_fld1: //Push +1.0 onto the FPU register stack
			case OP_fild: //Push m32int onto the FPU register stack.
			case OP_fldz: //Push +0.0 onto the FPU register stack.
				if (direction == FORWARD_ANALYSIS){
					update_fp_src(cinstr, disasm, line);
					update_tos(FP_PUSH, disasm, line, direction);
					update_fp_dest(cinstr, disasm, line);
				}
				else if (direction == BACKWARD_ANALYSIS){
					update_fp_dest(cinstr, disasm, line);
					update_tos(FP_PUSH, disasm, line, direction);
					update_fp_src(cinstr, disasm, line);
				}
				break;
			case OP_fst:
				update_fp_reg(cinstr, disasm, line);
				break;
			case OP_fstp:  //Copy ST(0) to m32fp and pop register stack.
			case OP_fistp:  //Store ST(0) in m32int and pop register stack. 
				if (direction == FORWARD_ANALYSIS){
					update_fp_reg(cinstr, disasm, line);
					update_tos(FP_POP, disasm, line, direction);
				}
				else if (direction == BACKWARD_ANALYSIS){
					update_tos(FP_POP, disasm, line, direction);
					update_fp_reg(cinstr, disasm, line);
				}
				break;
			case OP_fmul: //Multiply ST(0) by m32fp and store result in ST(0).
			case OP_fmulp:  //Multiply ST(i) by ST(0), store result in ST(i), and pop the register stack.
				if (cinstr->opcode == OP_fmulp && direction == BACKWARD_ANALYSIS){
					update_tos(FP_POP, disasm, line, direction);
				}
				update_fp_reg(cinstr, disasm, line);
				if (cinstr->opcode == OP_fmulp && direction == FORWARD_ANALYSIS){
					update_tos(FP_POP, disasm, line, direction);
				}
				break;
			case OP_fxch:
				update_fp_reg(cinstr, disasm, line);
			case OP_faddp:  //Add ST(0) to ST(i), store result in ST(i), and pop the register stack
			case OP_fadd:   //Add m32fp to ST(0) and store result in ST(0).
			case OP_fsubp:  //Subtract ST(0) from ST(1), store result in ST(1), and pop register stack.
			case OP_fsub:   //Subtract m32fp from ST(0) and store result in ST(0).
			case OP_fdivp:  //Divide ST(1) by ST(0), store result in ST(1), and pop the register stack.
			case OP_fdiv:   //Divide ST(0) by m32fp and store result in ST(0).
				if (((cinstr->opcode == OP_faddp) || (cinstr->opcode == OP_fsubp) || (cinstr->opcode == OP_fdivp)) && (direction == BACKWARD_ANALYSIS)){
					update_tos(FP_POP, disasm, line, direction);
				}
				update_fp_reg(cinstr, disasm, line);
				if (((cinstr->opcode == OP_faddp) || (cinstr->opcode == OP_fsubp) || (cinstr->opcode == OP_fdivp)) && (direction == FORWARD_ANALYSIS)){
					update_tos(FP_POP, disasm, line, direction);
				}
				break;
			case OP_fcomp:
				update_tos(FP_POP, disasm, line, direction);
				break;
			default:
				unhandled = true;
			}
			ASSERT_MSG((!unhandled), ("ERROR: opcode %s(%d) with %d dests and %d srcs (app_pc - %d) not handled in canonicalization\n", dr_operation_to_string(cinstr->opcode).c_str(), cinstr->opcode,
				cinstr->num_dsts, cinstr->num_srcs, cinstr->pc));
		}

	}


}

/*******************************  generic x86 analysis functions *****************************************************/
bool check_eflag_bit(eflag_bits flag_type, uint32_t reg_val){

	return ((reg_val & (1 << flag_type)) == (1 << flag_type));

}

bool check_lahf_bit(lahf_bits flag_type, uint32_t reg_val){

	uint32_t ah = (reg_val >> 8) & 0xFF;
	uint32_t al = reg_val & 0xFF;

	//cout << hex << ah << " " << al << endl;

	if (flag_type == Overflow_lahf){
		return al == 1;
	}
	else{
		return ((ah & (1 << flag_type)) == (1 << flag_type));
	}

}

/* checks if the particular instruction is handled 
Function - is_instr_handled

Parameters
 opcode - opcode of the x86 instruction

Return
 bool - whether the particular instruction is handled

*/
bool is_instr_handled(uint32_t opcode){

	switch (opcode){
	case OP_movaps:
	case OP_xorps:
	case OP_movss:
	case OP_pmuldq:
	case OP_push_imm:
	case OP_push:
	case OP_pop:
	case OP_mov_st:
	case OP_mov_ld:
	case OP_mov_imm:
	case OP_movzx:
	case OP_movsx:
	case OP_movq:
	case OP_movd:
	case OP_movapd:
	case OP_movdqa:
	case OP_cvttsd2si:
	case OP_imul:
	case OP_mul:
	case OP_idiv:
	case OP_cdq:
	case OP_xchg:
	case OP_xor:
	case OP_sub:
	case OP_pxor:
	case OP_psubd:
	case OP_add:
	case OP_and:
	case OP_or:
	case OP_andpd:
	case OP_neg:
	case OP_dec:
	case OP_inc:
	case OP_sar:
	case OP_shr:
	case OP_shl:
	case OP_psllq:
	case OP_psrlq:
	case OP_not:
	case OP_lea:
	case OP_sbb:
	case OP_setz:
	case OP_sets:
	case OP_setns:
	case OP_setb:
	case OP_cmp:
	case OP_test:
	case OP_jmp:
	case OP_jmp_short:
	case OP_jnl:
	case OP_jnl_short:
	case OP_jl:
	case OP_jnle:
	case OP_jnle_short:
	case OP_jnz:
	case OP_jnz_short:
	case OP_jz:
	case OP_jnb_short:
	case OP_jb_short:
	case OP_jz_short:
	case OP_jl_short:
	case OP_jns_short:
	case OP_js_short:
	case OP_jnbe_short:
	case OP_jle_short:
	case OP_jle:
	case OP_jbe_short:
	case OP_jns:
	case OP_jb:
	case OP_jnb:
	case OP_js:
	case OP_jmp_ind:
	case OP_call:
	case OP_ret:
	case OP_call_ind:
	case OP_enter:
	case OP_leave:
	case OP_fldcw:
	case OP_fnstcw:
	case OP_stmxcsr:
	case OP_nop:
	case OP_adc:
	case OP_cwde:

	case OP_cmovle:
	case OP_cmovnle:
	case OP_cmovl:
	case OP_cmovnl:
	case OP_cmovz:
	case OP_cmovnz:

		return true;
	default:
		return false;
	}

}

/* this returns the flag mask register of the affected flags by the relevant opcode
Function - is_eflags_affected

Parameters
 opcode - opcode of the x86 instruction

Return
 uint32_t - the mask register which carries a bit vector of which flags are affected by which instruction

*/
uint32_t is_eflags_affected(uint32_t opcode){

	uint32_t flags = 0;
	
	switch (opcode){
	case OP_imul:
	case OP_mul:
		//CF and OF
		SET_FLAG(flags, Carry_Flag);
		SET_FLAG(flags, Overflow_Flag); break;


    // all 6 flags
	case OP_sub:
	case OP_add:
	case OP_neg:
	case OP_sbb:
	case OP_cmp:
		//Op_cmp - CF, OF, SF, ZF, AF, and PF
		//Op_neg - CF 0 if opnd 0, OF, SF, ZF, AF, and PF
		//OF, SF, ZF, AF, CF, and PF
		SET_FLAG(flags, Overflow_Flag);
		SET_FLAG(flags, Sign_Flag);
		SET_FLAG(flags, Zero_Flag);
		SET_FLAG(flags, Carry_Flag);
		SET_FLAG(flags, Parity_Flag);
		SET_FLAG(flags, Auxiliary_Carry_Flag); break;
	
	// without AF
	case OP_test:
		//OF and CF flags are set to 0. The SF, ZF, and PF 
	case OP_sar:
	case OP_shr:
	case OP_shl:
		//OF, CF, SF, ZF, and PF (AF is generally undefined) - there are conditions for OF, CF as well but we will disregard them for now
	case OP_xor:
	case OP_and:
	case OP_or:
		//OF,CF cleared, SF, ZF, PF
		SET_FLAG(flags, Overflow_Flag);
		SET_FLAG(flags, Sign_Flag);
		SET_FLAG(flags, Zero_Flag);
		SET_FLAG(flags, Carry_Flag);
		SET_FLAG(flags, Parity_Flag); break;
	
	// without CF
	case OP_dec:
	case OP_inc:
		//OF, SF, ZF, AF, and PF
		SET_FLAG(flags, Overflow_Flag);
		SET_FLAG(flags, Sign_Flag);
		SET_FLAG(flags, Zero_Flag);
		SET_FLAG(flags, Parity_Flag);
		SET_FLAG(flags, Auxiliary_Carry_Flag); break;
	
	}

	return flags;

}

/* determines if the branch is taken based on the value of the flags register 
Function - is_branch_taken

Parameters
 opcode - x86 instruction opcode, make sure this instruction is a conditional jump
 flags  - the actual value of the flags recovered by DR instrumentation at runtime via lahf instruction (refer lahf_bits to find the exact position)

Return 
 bool - returns, based on the jump instruction and the flag values at the point of execution, whether this branch is taken or not
*/
bool is_branch_taken(uint32_t opcode, uint32_t flags){

	bool cf = check_lahf_bit(Carry_lahf, flags);
	bool zf = check_lahf_bit(Zero_lahf, flags);
	bool of = check_lahf_bit(Overflow_lahf, flags);
	bool af = check_lahf_bit(Auxiliary_lahf, flags);
	bool pf = check_lahf_bit(Parity_lahf, flags);
	bool sf = check_lahf_bit(Sign_lahf, flags);

	

	bool unhandled = false;
	//cout << "opcode " << opcode << endl;

	switch (opcode){
	
	case OP_jnl:
	case OP_jnl_short:
		//Jump short if not less(SF = OF)
		return of == sf;
	
	case OP_jl:
	case OP_jl_short:
		return of != sf;

	case OP_jnle:
	case OP_jnle_short:
		//Jump short if not less or equal (ZF=0 and SF=OF)
		return (of == sf) && !zf;

	case OP_jnz:
	case OP_jnz_short:
		return !zf;

	case OP_jz:
	case OP_jz_short:
		//ZF value
		return zf;

	case OP_jb:
	case OP_jb_short:
		return cf;

	case OP_jnb:
	case OP_jnb_short:
		//CF value
		return !cf;

	case OP_jns:
	case OP_jns_short:
		return !sf;
	case OP_js:
	case OP_js_short:
		//SF value
		return sf;

	case OP_jbe_short:
		//Jump short if below or equal (CF=1 or ZF=1)
		return cf || zf;
	
	case OP_jle:
	case OP_jle_short:
		//Jump near if less or equal (ZF=1 or SF~=OF)
		return zf || (sf != of);
	case OP_jnbe_short:
		//Jump short if not below or equal (CF=0 and ZF=0)
		//cout << "jnbe " << cf << " " << zf << endl;
		return !cf && !zf;
	case OP_sbb:
		return cf;
	case OP_jnbe:
		return !cf && !zf;

	case OP_cmovle:
		return zf || (sf != of);
	case OP_cmovnle:
		return !zf || (sf == of);
	case OP_cmovl:
		return (sf != of);
	case OP_cmovnl:
		return (sf == of);
	case OP_cmovz:
		return zf;
	case OP_cmovns:
		return !sf;
	case OP_cmovnz:
		return !zf;

	default:
		unhandled = true;
	}



	ASSERT_MSG((!unhandled), ("ERROR: is brach taken opcode %d not handled in canonicalization\n", opcode));

	return false;

}

/* checks if a particular conditional jump is affected 
Function - is_jmp_conditional_affected

Parameters
 opcode - x86 instruction opcode, make sure this instruction is a conditional jump
 flags	- the flags bit mask vector of the condition codes for a given instruction, obtained through is_eflags_affected function

Return
 bool - whether the condition codes that are updated by a particular instruction is actually consumed by the conditional jump. 

 However, note that it is up to the user to perform reaching definitions analysis to find the correct instruction which determines
 branching direction. This function will only say whether given the flags mask vector for a particular instruction, will it potentially 
 affect the branching decision for the conditional jump that is queried.

*/
bool is_jmp_conditional_affected(uint32_t opcode, uint32_t flags){

	bool cf = check_eflag_bit(Carry_Flag,flags);
	bool zf = check_eflag_bit(Zero_Flag, flags);
	bool of = check_eflag_bit(Overflow_Flag, flags);
	bool af = check_eflag_bit(Auxiliary_Carry_Flag, flags);
	bool pf = check_eflag_bit(Parity_Flag, flags);
	bool sf = check_eflag_bit(Sign_Flag, flags);

	bool unhandled = false;

	/*BUG - need to change it to a OR */

	switch (opcode){
	case OP_jnl:
	case OP_jnl_short:
	case OP_jl:
	case OP_jl_short:
		//Jump short if not less(SF = OF)
		return of && sf;
	case OP_jnle:
	case OP_jnle_short:
		//Jump short if not less or equal (ZF=0 and SF=OF)
		return of && sf && zf;
	case OP_jnz:
	case OP_jnz_short:
	case OP_jz:
	case OP_jz_short:
		//ZF value
		return zf;
	case OP_jb:
	case OP_jb_short:
	case OP_jnb:
	case OP_jnb_short:
		//CF value
		return cf;
	case OP_jns:
	case OP_jns_short:
	case OP_js:
	case OP_js_short:
		//SF value
		return sf;
	case OP_jbe_short:
		//Jump short if below or equal (CF=1 or ZF=1)
		return cf || zf;
	case OP_jle:
	case OP_jle_short:
		//Jump near if less or equal (ZF=1 or SF~=OF)
		return zf || (sf && of);
	case OP_jnbe_short:
		//Jump short if not below or equal (CF=0 and ZF=0)
		return cf && zf;
	case OP_sbb:
		return cf;
	case OP_jnbe:
		return cf && zf;

	case OP_cmovnle:
	case OP_cmovle:
		return zf || (sf && of);
	case OP_cmovl:
	case OP_cmovnl:
		return sf && of;
	case OP_cmovz:
		return zf;
	case OP_cmovns:
		return sf;
	case OP_cmovnz:
		return zf;

	default:
		unhandled = true;
	}

	ASSERT_MSG((!unhandled), ("ERROR: jmp affected opcode %d not handled in canonicalization\n", opcode));

	return false;

}

/*
Function - is_conditional_jump_ins

Parameters
	opcode -  x86 instruction opcode

Return
	bool - whether the instruction is a conditional jump instruction

*/
bool is_conditional_jump_ins(uint32_t opcode){


	switch (opcode){
	
	case OP_jnl:
	case OP_jnl_short:
	case OP_jl:
	case OP_jnle:
	case OP_jnle_short:
	case OP_jnz:
	case OP_jnz_short:
	case OP_jz:
	case OP_jnb_short:
	case OP_jb_short:
	case OP_jz_short:
	case OP_jl_short:
	case OP_jns_short:
	case OP_js_short:
	case OP_jnbe_short:
	case OP_jle_short:
	case OP_jle:
	case OP_jbe_short:
	case OP_jns:
	case OP_jb:
	case OP_jnb:
	case OP_js:
	case OP_jnbe:

	//sbb and other computation involving eflags
	case OP_sbb:
	case OP_adc:
	case OP_cmovnle:
	case OP_cmovle:
	case OP_cmovl:
	case OP_cmovnl:
	case OP_cmovz:
	case OP_cmovns:

		return true;
	default:
		return false;
	}

}

uint32_t dr_logical_to_operation(uint32_t opcode){

	switch (opcode){

	case OP_jnl:
	case OP_jnl_short:
		//Jump short if not less(SF = OF)
		return op_ge;

	case OP_jl:
	case OP_jl_short:
		return op_lt;

	case OP_jnle:
	case OP_jnle_short:
		//Jump short if not less or equal (ZF=0 and SF=OF)
		return op_gt;

	case OP_jnz:
	case OP_jnz_short:
		return op_neq;

	case OP_jz:
	case OP_jz_short:
		//ZF value
		return op_eq;

	case OP_jb:
	case OP_jb_short:
		return op_lt;

	case OP_jnb:
	case OP_jnb_short:
		//CF value
		return op_ge;

		/* check these */
	case OP_jns:
	case OP_jns_short:
		return op_ge;
	case OP_js:
	case OP_js_short:
		//SF value
		return op_lt;

	case OP_jbe_short:
		//Jump short if below or equal (CF=1 or ZF=1)
		return op_le;

	case OP_jle:
	case OP_jle_short:
		//Jump near if less or equal (ZF=1 or SF~=OF)
		return op_lt;
	case OP_jnbe_short:
		//Jump short if not below or equal (CF=0 and ZF=0)
		//cout << "jnbe " << cf << " " << zf << endl;
		return op_gt;
	case OP_sbb:
		return op_lt;
	case OP_jnbe:
		return op_gt;

	}

	return op_unknown;


}

static void populate_rinstr(rinstr_t * rinstr, operand_t dst, int num_srcs, operand_t src1, operand_t src2, int operation, bool sign){

	rinstr->dst = dst; 
	rinstr->num_srcs = num_srcs;
	rinstr->srcs[0] = src1;
	if(num_srcs > 1){
		rinstr->srcs[1] = src2;
	}
	rinstr->operation = operation;
	rinstr->sign = sign;


}

/******************************** debug routines **************************************************************/
static void assert_opnds(int opcode, int needed_src, int needed_dst, int actual_src, int actual_dst){
	ASSERT_MSG((needed_src == actual_src) && (needed_dst == actual_src), ("ERROR: opcode %d - needed %d(src) %d(dst), actual %d(src) %d(dst)\n", opcode, needed_src, needed_dst, actual_src, actual_dst));
}

void print_rinstrs(ostream &file, rinstr_t * rinstr, int amount){
	file << "canonicalized instrs:" << endl;
	for (int j = 0; j < amount; j++){
		file << opnd_to_string(&rinstr[j].dst) << " = ";
		if (rinstr[j].operation != op_assign){
			file << operation_to_string(rinstr[j].operation) << " ";
		}
		for (int i = 0; i < rinstr[j].num_srcs; i++){
		 file << opnd_to_string(&rinstr[j].srcs[i]) << " ";
		}
		file << endl;
	}
	
}

void print_cinstr(cinstr_t * instr){
	cout << instr->opcode << ",";
	cout << instr->num_dsts << ",";
	for (int i = 0; i < instr->num_dsts; i++){
		cout << instr->dsts[i].type << ",";
		cout << instr->dsts[i].width << ",";
		cout << instr->dsts[i].value << ",";
	}

	cout << instr->num_srcs << ",";
	for (int i = 0; i < instr->num_srcs; i++){
		cout << instr->srcs[i].type << ",";
		cout << instr->srcs[i].width << ",";
		cout << instr->srcs[i].value << ",";
	}
	cout << instr->eflags << ",";
	cout << instr->pc << endl;

	cout << dr_operation_to_string(instr->opcode) << endl;
}


