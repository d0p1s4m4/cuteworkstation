#ifndef CPU_H
# define CPU_H 1

# include <stdint.h>

typedef struct
{
  uint64_t gpr[32];
  uint64_t pc;

  uint64_t fpr[32];
} Cpu;

#endif /* CPU_H */
