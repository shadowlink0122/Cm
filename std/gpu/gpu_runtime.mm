// gpu_runtime.mm - Cm GPU(Metal) stdバッキング実装
// Apple Metal Framework を使用したGPGPUコンピュート
// M1以上のApple Silicon + macOSで動作

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

// ============================================================
// 内部構造体
// ============================================================

// Metalデバイスコンテキスト
typedef struct {
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
} GpuDevice;

// Metalバッファ
typedef struct {
  id<MTLBuffer> buffer;
  size_t size;
} GpuBuffer;

// Metalカーネル（コンピュートパイプライン）
typedef struct {
  id<MTLComputePipelineState> pipeline;
  id<MTLDevice> device;
  id<MTLCommandQueue> commandQueue;
} GpuKernel;

extern "C" {

// ============================================================
// デバイス管理
// ============================================================

// デフォルトGPUデバイスを作成
// 戻り値: GpuDevice*（失敗時は0）
int64_t gpu_device_create() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
      fprintf(stderr, "[gpu] Metal デバイスが見つかりません\n");
      return 0;
    }

    GpuDevice *ctx = (GpuDevice *)malloc(sizeof(GpuDevice));
    ctx->device = device;
    ctx->commandQueue = [device newCommandQueue];

    if (!ctx->commandQueue) {
      fprintf(stderr, "[gpu] コマンドキュー作成に失敗\n");
      free(ctx);
      return 0;
    }

    return (int64_t)ctx;
  }
}

// デバイスを破棄
void gpu_device_destroy(int64_t device_handle) {
  if (device_handle == 0)
    return;
  GpuDevice *ctx = (GpuDevice *)device_handle;
  ctx->commandQueue = nil;
  ctx->device = nil;
  free(ctx);
}

// デバイス名を取得（デバッグ用）
// 戻り値: デバイス名の文字列ポインタ
const char *gpu_device_name(int64_t device_handle) {
  if (device_handle == 0)
    return "unknown";
  GpuDevice *ctx = (GpuDevice *)device_handle;
  return [[ctx->device name] UTF8String];
}

// ============================================================
// バッファ管理
// ============================================================

// GPUバッファを作成
// size: バイト単位のサイズ
// 戻り値: GpuBuffer*（失敗時は0）
int64_t gpu_buffer_create(int64_t device_handle, int64_t size) {
  if (device_handle == 0 || size <= 0)
    return 0;
  GpuDevice *ctx = (GpuDevice *)device_handle;

  @autoreleasepool {
    id<MTLBuffer> buffer =
        [ctx->device newBufferWithLength:(NSUInteger)size
                                 options:MTLResourceStorageModeShared];
    if (!buffer)
      return 0;

    GpuBuffer *buf = (GpuBuffer *)malloc(sizeof(GpuBuffer));
    buf->buffer = buffer;
    buf->size = (size_t)size;
    return (int64_t)buf;
  }
}

// GPUバッファを破棄
void gpu_buffer_destroy(int64_t buffer_handle) {
  if (buffer_handle == 0)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  buf->buffer = nil;
  free(buf);
}

// CPUからGPUバッファにデータを書き込み
void gpu_buffer_write(int64_t buffer_handle, void *data, int64_t size) {
  if (buffer_handle == 0 || data == NULL)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  size_t copy_size = (size_t)size;
  if (copy_size > buf->size)
    copy_size = buf->size;
  memcpy([buf->buffer contents], data, copy_size);
}

// GPUバッファからCPUにデータを読み取り
void gpu_buffer_read(int64_t buffer_handle, void *data, int64_t size) {
  if (buffer_handle == 0 || data == NULL)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  size_t copy_size = (size_t)size;
  if (copy_size > buf->size)
    copy_size = buf->size;
  memcpy(data, [buf->buffer contents], copy_size);
}

// ============================================================
// Long バッファ操作（JITエイリアス分析回避版）
// ============================================================
// JITモードではスタック配列のvoid*渡しでエイリアス問題が発生するため
// long*型で直接受け取る版を提供

// Cm long配列をGPUバッファに書き込み
void gpu_buffer_write_longs(int64_t buffer_handle, int64_t *data,
                            int64_t count) {
  if (buffer_handle == 0 || data == NULL || count <= 0)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  size_t copy_size = (size_t)(count * sizeof(int64_t));
  if (copy_size > buf->size)
    copy_size = buf->size;
  memcpy([buf->buffer contents], data, copy_size);
}

// GPUバッファからCm long配列に読み取り
void gpu_buffer_read_longs(int64_t buffer_handle, int64_t *data,
                           int64_t count) {
  if (buffer_handle == 0 || data == NULL || count <= 0)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  size_t copy_size = (size_t)(count * sizeof(int64_t));
  if (copy_size > buf->size)
    copy_size = buf->size;
  memcpy(data, [buf->buffer contents], copy_size);
}

// ============================================================
// Float バッファ操作（Cm long ↔ GPU float 変換）
// ============================================================
// Cmにはfloat型がないため、longの下位32bitにfloat bit patternを格納。
// 書き込み時: Cm long配列 → float配列に変換してGPUバッファへ
// 読み取り時: GPUバッファのfloat配列 → Cm long配列に変換

// floatバッファ作成 (count個のfloat = count * 4 bytes)
int64_t gpu_buffer_create_floats(int64_t device_handle, int64_t count) {
  return gpu_buffer_create(device_handle, count * sizeof(float));
}

// Cm long配列をfloat配列に変換してGPUバッファに書き込み
// int_data: long配列 (各要素の下位32bitがfloat bit pattern)
// count: 要素数
void gpu_buffer_write_floats(int64_t buffer_handle, int64_t *int_data,
                             int64_t count) {
  if (buffer_handle == 0 || int_data == NULL || count <= 0)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  float *gpu_data = (float *)[buf->buffer contents];
  for (int64_t i = 0; i < count; i++) {
    // longの下位32bitをfloatのbit patternとして解釈
    uint32_t bits = (uint32_t)(int_data[i] & 0xFFFFFFFF);
    memcpy(&gpu_data[i], &bits, sizeof(float));
  }
}

// GPUバッファのfloat配列をCm long配列に変換して読み出し
// int_data: long配列 (各要素にfloat bit patternが格納される)
// count: 要素数
void gpu_buffer_read_floats(int64_t buffer_handle, int64_t *int_data,
                            int64_t count) {
  if (buffer_handle == 0 || int_data == NULL || count <= 0)
    return;
  GpuBuffer *buf = (GpuBuffer *)buffer_handle;
  float *gpu_data = (float *)[buf->buffer contents];
  for (int64_t i = 0; i < count; i++) {
    uint32_t bits;
    memcpy(&bits, &gpu_data[i], sizeof(float));
    int_data[i] = (int64_t)bits;
  }
}

// float→long変換ヘルパー (Cm側で使用)
// float値をlong (下位32bit = IEEE754 bit pattern) に変換
int64_t gpu_float_to_bits(double value) {
  float f = (float)value;
  uint32_t bits;
  memcpy(&bits, &f, sizeof(float));
  return (int64_t)bits;
}

// long→float変換ヘルパー (結果の読み取り用)
// IEEE754 bit pattern → double値として返す
double gpu_bits_to_float(int64_t bits) {
  uint32_t u = (uint32_t)(bits & 0xFFFFFFFF);
  float f;
  memcpy(&f, &u, sizeof(float));
  return (double)f;
}

// ============================================================
// カーネル（コンピュートシェーダー）管理
// ============================================================

// Metal Shading Language ソースからカーネルを作成
// source: MSLソースコード文字列
// func_name: カーネル関数名
// 戻り値: GpuKernel*（失敗時は0）
int64_t gpu_kernel_create(int64_t device_handle, const char *source,
                          const char *func_name) {
  if (device_handle == 0 || source == NULL || func_name == NULL)
    return 0;
  GpuDevice *ctx = (GpuDevice *)device_handle;

  @autoreleasepool {
    NSError *error = nil;
    NSString *sourceStr = [NSString stringWithUTF8String:source];
    MTLCompileOptions *options = [[MTLCompileOptions alloc] init];

    // MSLソースをコンパイル
    id<MTLLibrary> library = [ctx->device newLibraryWithSource:sourceStr
                                                       options:options
                                                         error:&error];
    if (!library) {
      fprintf(stderr, "[gpu] シェーダーコンパイルエラー: %s\n",
              [[error localizedDescription] UTF8String]);
      return 0;
    }

    // カーネル関数を取得
    NSString *funcName = [NSString stringWithUTF8String:func_name];
    id<MTLFunction> function = [library newFunctionWithName:funcName];
    if (!function) {
      fprintf(stderr, "[gpu] カーネル関数 '%s' が見つかりません\n", func_name);
      return 0;
    }

    // コンピュートパイプライン作成
    id<MTLComputePipelineState> pipeline =
        [ctx->device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline) {
      fprintf(stderr, "[gpu] パイプライン作成エラー: %s\n",
              [[error localizedDescription] UTF8String]);
      return 0;
    }

    GpuKernel *kernel = (GpuKernel *)malloc(sizeof(GpuKernel));
    kernel->pipeline = pipeline;
    kernel->device = ctx->device;
    kernel->commandQueue = ctx->commandQueue;
    return (int64_t)kernel;
  }
}

// カーネルを破棄
void gpu_kernel_destroy(int64_t kernel_handle) {
  if (kernel_handle == 0)
    return;
  GpuKernel *kernel = (GpuKernel *)kernel_handle;
  kernel->pipeline = nil;
  kernel->device = nil;
  kernel->commandQueue = nil;
  free(kernel);
}

// ============================================================
// カーネル実行
// ============================================================

// 3バッファ版ディスパッチ（ベクトル加算等に最適）
// buf_a, buf_b: 入力バッファ, buf_out: 出力バッファ
// count: 要素数
void gpu_dispatch(int64_t kernel_handle, int64_t buf_a_handle,
                  int64_t buf_b_handle, int64_t buf_out_handle, int64_t count) {
  if (kernel_handle == 0 || count <= 0)
    return;

  GpuKernel *kernel = (GpuKernel *)kernel_handle;
  GpuBuffer *buf_a = (GpuBuffer *)buf_a_handle;
  GpuBuffer *buf_b = (GpuBuffer *)buf_b_handle;
  GpuBuffer *buf_out = (GpuBuffer *)buf_out_handle;

  @autoreleasepool {
    id<MTLCommandBuffer> commandBuffer = [kernel->commandQueue commandBuffer];
    id<MTLComputeCommandEncoder> encoder =
        [commandBuffer computeCommandEncoder];

    [encoder setComputePipelineState:kernel->pipeline];

    // バッファをバインド
    if (buf_a)
      [encoder setBuffer:buf_a->buffer offset:0 atIndex:0];
    if (buf_b)
      [encoder setBuffer:buf_b->buffer offset:0 atIndex:1];
    if (buf_out)
      [encoder setBuffer:buf_out->buffer offset:0 atIndex:2];

    // スレッドグループサイズを計算
    NSUInteger threadGroupSize = kernel->pipeline.maxTotalThreadsPerThreadgroup;
    if (threadGroupSize > (NSUInteger)count) {
      threadGroupSize = (NSUInteger)count;
    }

    MTLSize gridSize = MTLSizeMake((NSUInteger)count, 1, 1);
    MTLSize groupSize = MTLSizeMake(threadGroupSize, 1, 1);

    [encoder dispatchThreads:gridSize threadsPerThreadgroup:groupSize];
    [encoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    // エラーチェック
    if ([commandBuffer error]) {
      fprintf(stderr, "[gpu] 実行エラー: %s\n",
              [[[commandBuffer error] localizedDescription] UTF8String]);
    }
  }
}

// 1バッファ版ディスパッチ（インプレース操作等）
void gpu_dispatch_1(int64_t kernel_handle, int64_t buf_handle, int64_t count) {
  gpu_dispatch(kernel_handle, buf_handle, 0, 0, count);
}

// 2バッファ版ディスパッチ（入力+出力）
void gpu_dispatch_2(int64_t kernel_handle, int64_t buf_in_handle,
                    int64_t buf_out_handle, int64_t count) {
  gpu_dispatch(kernel_handle, buf_in_handle, buf_out_handle, 0, count);
}

// N個のバッファ版ディスパッチ（ニューラルネット等の複雑なカーネル用）
// buffers: バッファハンドルの配列, buffer_count: バッファ数
void gpu_dispatch_n(int64_t kernel_handle, int64_t *buffers,
                    int64_t buffer_count, int64_t count) {
  if (kernel_handle == 0 || count <= 0)
    return;

  GpuKernel *kernel = (GpuKernel *)kernel_handle;

  @autoreleasepool {
    id<MTLCommandBuffer> commandBuffer = [kernel->commandQueue commandBuffer];
    id<MTLComputeCommandEncoder> encoder =
        [commandBuffer computeCommandEncoder];

    [encoder setComputePipelineState:kernel->pipeline];

    // N個のバッファをバインド
    for (int64_t i = 0; i < buffer_count; i++) {
      if (buffers[i] != 0) {
        GpuBuffer *buf = (GpuBuffer *)buffers[i];
        [encoder setBuffer:buf->buffer offset:0 atIndex:(NSUInteger)i];
      }
    }

    // スレッドグループサイズを計算
    NSUInteger threadGroupSize = kernel->pipeline.maxTotalThreadsPerThreadgroup;
    if (threadGroupSize > (NSUInteger)count) {
      threadGroupSize = (NSUInteger)count;
    }

    MTLSize gridSize = MTLSizeMake((NSUInteger)count, 1, 1);
    MTLSize groupSize = MTLSizeMake(threadGroupSize, 1, 1);

    [encoder dispatchThreads:gridSize threadsPerThreadgroup:groupSize];
    [encoder endEncoding];

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];

    // エラーチェック
    if ([commandBuffer error]) {
      fprintf(stderr, "[gpu] 実行エラー: %s\n",
              [[[commandBuffer error] localizedDescription] UTF8String]);
    }
  }
}

} // extern "C"
