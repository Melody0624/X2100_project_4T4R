/*
 * Format of an (MIPS)instruction in memory.
 *
 */
#ifndef __MIPS_ASM_INST_H__
#define __MIPS_ASM_INST_H__


/*
 * Major opcodes; before MIPS IV cop1x was called cop3.
 */
enum major_op {
    spec_op, bcond_op, j_op, jal_op,
    beq_op, bne_op, blez_op, bgtz_op,
    addi_op, addiu_op, slti_op, sltiu_op,
    andi_op, ori_op, xori_op, lui_op,
    cop0_op, cop1_op, cop2_op, cop1x_op,
    beql_op, bnel_op, blezl_op, bgtzl_op,
    daddi_op, daddiu_op, ldl_op, ldr_op,
    spec2_op, jalx_op, mdmx_op, spec3_op,
    lb_op, lh_op, lwl_op, lw_op,
    lbu_op, lhu_op, lwr_op, lwu_op,
    sb_op, sh_op, swl_op, sw_op,
    sdl_op, sdr_op, swr_op, cache_op,
    ll_op, lwc1_op, lwc2_op, pref_op,
    lld_op, ldc1_op, ldc2_op, ld_op,
    sc_op, swc1_op, swc2_op, major_3b_op,
    scd_op, sdc1_op, sdc2_op, sd_op
};


struct j_format {
    unsigned int target : 26;
    unsigned int opcode : 6; /* Jump format */
};

struct i_format {           /* signed immediate format */
    signed int simmediate : 16;
    unsigned int rt : 5;
    unsigned int rs : 5;
    unsigned int opcode : 6;
};

struct u_format {           /* unsigned immediate format */
    unsigned int uimmediate : 16;
    unsigned int rt : 5;
    unsigned int rs : 5;
    unsigned int opcode : 6;
};

struct c_format {           /* Cache (>= R6000) format */
    unsigned int simmediate : 16;
    unsigned int cache : 2;
    unsigned int c_op : 3;
    unsigned int rs : 5;
    unsigned int opcode : 6;
};

struct r_format {           /* Register format */
    unsigned int func : 6;
    unsigned int re : 5;
    unsigned int rd : 5;
    unsigned int rt : 5;
    unsigned int rs : 5;
    unsigned int opcode : 6;
};

union mips_instruction {
    unsigned int word;
    unsigned short halfword[2];
    unsigned char byte[4];
    struct j_format j_format;
    struct i_format i_format;
    struct u_format u_format;
    struct c_format c_format;
    struct r_format r_format;
};

#endif /* end of __MIPS_ASM_INST_H__ */
