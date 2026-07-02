/*
 * bsp_error.h
 * Error handling and assertions.
 */
#ifndef BSP_ERROR_H
#define BSP_ERROR_H

#ifdef __cplusplus
extern "C" {
#endif

void BSP_Error_Handler(void);
void BSP_Assert_Failed(const char *file, int line);

#define BSP_ASSERT(expr) \
    do { if (!(expr)) BSP_Assert_Failed(__FILE__, __LINE__); } while (0)

#ifdef __cplusplus
}
#endif

#endif /* BSP_ERROR_H */
