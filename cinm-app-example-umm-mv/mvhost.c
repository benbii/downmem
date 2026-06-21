#include <dpu.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// DPU 端 mv_dimm8_nopt 的固定参数
#define DPU_BLOCK_SIZE_INT 512                   // DPU每次处理的int数量
#define DPU_BLOCK_SIZE_BYTES (DPU_BLOCK_SIZE_INT * sizeof(int)) // 2048 bytes
#define DPU_RESULT_SIZE_BYTES 8                  // 结果 2个int
// 修正后的步长：A(2048) + B(2048) + Res(8) = 4104
#define DPU_TASKLET_STRIDE_BYTES 4104            
// 为了对齐方便，我们在Host端申请内存时可能需要按8字节对齐，但写入偏移必须严格遵守DPU代码
#define DPU_TASKLET_STRIDE_INT (DPU_TASKLET_STRIDE_BYTES / sizeof(int))

int main(int argc, char **argv) {
  struct dpu_set_t dpu_set, dpu;
  
  if (argc < 4) {
      printf("Usage: %s <num_dpus> <total_elements> <binary_path>\n", argv[0]);
      return -1;
  }

  int num_dpus = atoi(argv[1]);
  int vector_size = atoi(argv[2]);
  
  // 1. 计算每个 DPU 需要处理多少个 Tasklet 的工作量
  // mv_dimm8_nopt 固定处理 512 个元素，所以总元素必须是 512 的倍数
  if (vector_size % 512 != 0) {
      fprintf(stderr, "Error: vector_size must be a multiple of 512 for this benchmark.\n");
      return -1;
  }
  
  int total_tasklets = vector_size / 512;
  int tasklets_per_dpu = (total_tasklets + num_dpus - 1) / num_dpus; // 向上取整
  
  // 实际上每个 DPU 处理的数据量 (int)
  // 注意：这里包含了 A 和 B 以及结果的空间空洞，不仅仅是有效数据
  // 我们在 Host 端构建一个完整的 MRAM 镜像
  int mram_bytes_per_dpu = tasklets_per_dpu * DPU_TASKLET_STRIDE_BYTES;
  // 传输时通常要求 8 字节对齐，4104 已经是 8 的倍数
  
  // 2. 准备 Host 端数据
  int *A = malloc(vector_size * sizeof(int));
  int *B = malloc(vector_size * sizeof(int));
  int64_t *C_ref = malloc(total_tasklets * sizeof(int64_t)); // 参考结果

  // 初始化 A 和 B
  for (int i = 0; i < vector_size; i++) {
      A[i] = i;        // 示例数据
      B[i] = 1;        // 示例数据，设为1方便验证 (Result = Sum(A))
  }

  // 计算 CPU 参考结果 (Reference)
  // 注意：DPU代码逻辑是 vector A * vector B (点积)，每个 tasklet 输出一个 scalar
  for (int t = 0; t < total_tasklets; t++) {
      C_ref[t] = 0;
      for (int k = 0; k < 512; k++) {
          int idx = t * 512 + k;
          if (idx < vector_size) {
            C_ref[t] += A[idx] * B[idx];
          }
      }
  }

  DPU_ASSERT(dpu_alloc(num_dpus, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, argv[3], NULL));

  // 3. 构建交错的 MRAM 缓冲区 (Interleaved Buffer)
  // 这是一个巨大的 1D 数组，模拟 DPU 内部的内存布局：
  // [ T0_A (2048B) | T0_B (2048B) | T0_Res (8B) ] [ T1_A ... ]
  uint8_t *send_buffer = calloc(num_dpus, mram_bytes_per_dpu); //清零初始化

  int i = 0;
  DPU_FOREACH(dpu_set, dpu, i) {
      uint8_t *dpu_ptr = send_buffer + (i * mram_bytes_per_dpu);
      
      for (int t = 0; t < tasklets_per_dpu; t++) {
          int global_tasklet_idx = i * tasklets_per_dpu + t;
          
          if (global_tasklet_idx * 512 >= vector_size) break; // 超出数据范围

          // 计算在当前 DPU 缓冲区内的偏移
          int offset_base = t * DPU_TASKLET_STRIDE_BYTES;
          
          // 拷贝 Vector A 片段
          memcpy(dpu_ptr + offset_base, 
                 &A[global_tasklet_idx * 512], 
                 DPU_BLOCK_SIZE_BYTES);

          // 拷贝 Vector B 片段 (偏移 2048)
          memcpy(dpu_ptr + offset_base + DPU_BLOCK_SIZE_BYTES, 
                 &B[global_tasklet_idx * 512], 
                 DPU_BLOCK_SIZE_BYTES);
          
          // 结果区域 (offset_base + 4096) 留空，DPU 会写这里
      }
      
      // 发送整个缓冲区到 DPU
      DPU_ASSERT(dpu_prepare_xfer(dpu, dpu_ptr));
  }
  
  // 一次性 Push 整个布局
  printf("Transferring data to DPUs (Interleaved A and B)...\n");
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, mram_bytes_per_dpu, DPU_XFER_DEFAULT));

  // 4. 执行
  printf("Launching DPUs...\n");
  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));

  // 5. 取回结果
  // 同样，我们需要把整个镜像取回来，然后从中提取结果
  // 因为结果是稀疏分布的 (每 4104 字节里只有 8 字节结果)，DPU SDK 不支持 stride read。
  uint8_t *recv_buffer = calloc(num_dpus, mram_bytes_per_dpu);
  
  i = 0;
  DPU_FOREACH(dpu_set, dpu, i) {
      DPU_ASSERT(dpu_prepare_xfer(dpu, recv_buffer + (i * mram_bytes_per_dpu)));
  }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0, mram_bytes_per_dpu, DPU_XFER_DEFAULT));

  // 6. 验证结果
  int errors = 0;
  for (int i = 0; i < num_dpus; i++) {
      uint8_t *dpu_ptr = recv_buffer + (i * mram_bytes_per_dpu);
      
      for (int t = 0; t < tasklets_per_dpu; t++) {
          int global_tasklet_idx = i * tasklets_per_dpu + t;
          if (global_tasklet_idx * 512 >= vector_size) break;

          // 提取结果：偏移 = t * 4104 + 4096
          int offset_res = t * DPU_TASKLET_STRIDE_BYTES + 2 * DPU_BLOCK_SIZE_BYTES;
          
          // 注意：DPU 写入的是 2 个 int，我们取第一个作为结果 (long long accumulation split?)
          // 原代码 mv_dimm8_nopt 用 int32 v12[2]，只累加到了 v12[0]。
          // 所以我们只读 int32
          int32_t *res_ptr = (int32_t*)(dpu_ptr + offset_res);
          int32_t dpu_result = res_ptr[0];

          if (dpu_result != (int32_t)C_ref[global_tasklet_idx]) {
              printf("Error at DPU %d Tasklet %d: Expected %ld, Got %d\n", 
                     i, t, C_ref[global_tasklet_idx], dpu_result);
              errors++;
          } else {
              // printf("DPU %d Tasklet %d: OK (%d)\n", i, t, dpu_result);
          }
      }
  }

  if (errors == 0) {
    printf("Matrix-vector multiplication completed successfully with %d tasklets!\n", total_tasklets);
  } else {
    printf("Found %d errors\n", errors);
  }

  free(A); free(B); free(C_ref); free(send_buffer); free(recv_buffer);
  DPU_ASSERT(dpu_free(dpu_set));  
  return errors;  
}