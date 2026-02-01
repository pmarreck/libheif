/*
  Stress test for HEIC decoding with explicit thread control.

  This test decodes an HEIC file repeatedly with num_codec_threads=0
  to verify single-threaded decoding works correctly.

  Build: g++ -std=c++17 -o stress_test stress_test_decode.cc -lheif
  Run: ./stress_test <heic_file> [iterations]
*/

#include <libheif/heif.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define DEFAULT_ITERATIONS 100

struct ThreadArg {
    const char* filename;
    int thread_id;
    int iterations;
    int success_count;
    int fail_count;
    char error_msg[256];
};

void* decode_thread(void* arg) {
    ThreadArg* targ = (ThreadArg*)arg;
    targ->success_count = 0;
    targ->fail_count = 0;
    targ->error_msg[0] = '\0';

    for (int i = 0; i < targ->iterations; i++) {
        // Read file
        FILE* f = fopen(targ->filename, "rb");
        if (!f) {
            snprintf(targ->error_msg, sizeof(targ->error_msg), "Cannot open file");
            targ->fail_count++;
            continue;
        }

        fseek(f, 0, SEEK_END);
        size_t size = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint8_t* data = (uint8_t*)malloc(size);
        if (!data) {
            fclose(f);
            targ->fail_count++;
            continue;
        }

        fread(data, 1, size, f);
        fclose(f);

        // Create context
        heif_context* ctx = heif_context_alloc();
        if (!ctx) {
            free(data);
            targ->fail_count++;
            continue;
        }

        // Read from memory
        heif_error err = heif_context_read_from_memory_without_copy(ctx, data, size, NULL);
        if (err.code != heif_error_Ok) {
            heif_context_free(ctx);
            free(data);
            targ->fail_count++;
            continue;
        }

        // Get primary image handle
        heif_image_handle* handle = NULL;
        err = heif_context_get_primary_image_handle(ctx, &handle);
        if (err.code != heif_error_Ok || !handle) {
            heif_context_free(ctx);
            free(data);
            targ->fail_count++;
            continue;
        }

        // Allocate decoding options with num_codec_threads=0 (single-threaded)
        heif_decoding_options* options = heif_decoding_options_alloc();
        if (options) {
            options->num_codec_threads = 0;  // Force single-threaded decode
        }

        // Decode image
        heif_image* image = NULL;
        err = heif_decode_image(handle, &image, heif_colorspace_RGB, heif_chroma_interleaved_RGBA, options);

        if (options) {
            heif_decoding_options_free(options);
        }

        if (err.code != heif_error_Ok || !image) {
            snprintf(targ->error_msg, sizeof(targ->error_msg),
                     "Decode failed: %s (iteration %d)", err.message ? err.message : "unknown", i);
            heif_image_handle_release(handle);
            heif_context_free(ctx);
            free(data);
            targ->fail_count++;
            continue;
        }

        // Success - clean up
        heif_image_release(image);
        heif_image_handle_release(handle);
        heif_context_free(ctx);
        free(data);

        targ->success_count++;

        if ((i + 1) % 10 == 0) {
            printf("Thread %d: %d/%d iterations complete\n", targ->thread_id, i + 1, targ->iterations);
            fflush(stdout);
        }
    }

    return NULL;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <heic_file> [iterations] [threads]\n", argv[0]);
        fprintf(stderr, "  iterations: number of decode cycles per thread (default: %d)\n", DEFAULT_ITERATIONS);
        fprintf(stderr, "  threads: number of concurrent decode threads (default: 4)\n");
        return 1;
    }

    const char* filename = argv[1];
    int iterations = (argc > 2) ? atoi(argv[2]) : DEFAULT_ITERATIONS;
    int num_threads = (argc > 3) ? atoi(argv[3]) : 4;

    printf("Stress testing HEIC decode:\n");
    printf("  File: %s\n", filename);
    printf("  Iterations per thread: %d\n", iterations);
    printf("  Threads: %d\n", num_threads);
    printf("  num_codec_threads: 0 (single-threaded decode)\n");
    printf("\n");

    // Initialize libheif
    heif_init(NULL);

    // Create threads
    pthread_t* threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t));
    ThreadArg* args = (ThreadArg*)malloc(num_threads * sizeof(ThreadArg));

    for (int i = 0; i < num_threads; i++) {
        args[i].filename = filename;
        args[i].thread_id = i;
        args[i].iterations = iterations;
        pthread_create(&threads[i], NULL, decode_thread, &args[i]);
    }

    // Wait for all threads
    int total_success = 0;
    int total_fail = 0;

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        total_success += args[i].success_count;
        total_fail += args[i].fail_count;

        if (args[i].error_msg[0]) {
            printf("Thread %d error: %s\n", i, args[i].error_msg);
        }
    }

    printf("\nResults:\n");
    printf("  Total success: %d\n", total_success);
    printf("  Total fail: %d\n", total_fail);
    printf("  Success rate: %.2f%%\n", 100.0 * total_success / (total_success + total_fail));

    free(threads);
    free(args);

    heif_deinit();

    return (total_fail > 0) ? 1 : 0;
}
