#ifndef TESTS_H
#define TESTS_H
void run_baseline_suite(void);
void run_operand_form_suite(void);
void run_edge_mmx(void);
void run_edge_sse_fp(void);
void run_edge_sse_packed(void);
void run_edge_mxcsr(void);
void run_edge_memory(void);
void run_edge_immediates(void);
void run_edge_registers(void);
void run_edge_state(void);
void run_edge_fault_gating(void);
#endif
