#ifndef TESTS_MOCK_LWIP_SYS_H
#define TESTS_MOCK_LWIP_SYS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef int8_t s8_t;
typedef int16_t s16_t;
typedef int32_t s32_t;

typedef s8_t err_t;
#define ERR_OK          0
#define ERR_MEM        -1
#define ERR_BUF        -2
#define ERR_TIMEOUT    -3
#define ERR_RTE        -4
#define ERR_INPROGRESS -5
#define ERR_VAL        -6
#define ERR_WOULDBLOCK -7
#define ERR_USE        -8
#define ERR_ALREADY    -9
#define ERR_ISCONN     -10
#define ERR_CONN       -11
#define ERR_IF         -12
#define ERR_ABRT       -13
#define ERR_RST        -14
#define ERR_CLSD       -15
#define ERR_ARG        -16

#define SYS_ARCH_TIMEOUT 0xffffffffUL

struct sys_sem_record {
    uint32_t count;
    bool valid;
    uint32_t magic;
};

typedef struct sys_sem_record* sys_sem_t;

#define SYS_SEM_MAGIC 0x53454D31

err_t sys_sem_new(sys_sem_t* sem, u8_t count);
void sys_sem_signal(sys_sem_t* sem);
u32_t sys_arch_sem_wait(sys_sem_t* sem, u32_t timeout);
void sys_sem_wait(sys_sem_t* sem);
void sys_sem_free(sys_sem_t* sem);
int sys_sem_valid(sys_sem_t* sem);
void sys_sem_set_invalid(sys_sem_t* sem);

#ifdef __cplusplus
}
#endif

#endif // TESTS_MOCK_LWIP_SYS_H
