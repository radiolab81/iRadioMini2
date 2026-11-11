/* Minimale POSIX-Nachbildung der benutzten FreeRTOS-/esp_timer-Funktionen (1 Tick = 1 ms). Nur fuer den Host-Test. */
#define _GNU_SOURCE
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

int64_t esp_timer_get_time(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000 + t.tv_nsec / 1000; }

/* Tasks */
typedef struct { void (*fn)(void *); void *arg; } tstart_t;
static void *tramp(void *p) { tstart_t t = *(tstart_t *)p; free(p); t.fn(t.arg); return NULL; }
BaseType_t xTaskCreatePinnedToCore(void (*fn)(void *), const char *n, uint32_t st, void *arg, int prio, TaskHandle_t *h, int core)
{
    (void)n; (void)st; (void)prio; (void)core;
    tstart_t *t = malloc(sizeof *t); t->fn = fn; t->arg = arg;
    pthread_t th; pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &a, tramp, t)) return 0;
    if (h) *h = (void *)th;
    return pdPASS;
}
void vTaskDelete(TaskHandle_t t) { (void)t; pthread_exit(NULL); }   /* nur vTaskDelete(NULL) wird benutzt */
void vTaskDelay(TickType_t ms) { usleep((useconds_t)(ms ? ms : 1) * 1000); }

/* Mutex */
SemaphoreHandle_t xSemaphoreCreateMutex(void) { pthread_mutex_t *m = malloc(sizeof *m); pthread_mutex_init(m, NULL); return m; }
BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t t) { (void)t; return pthread_mutex_lock(s) == 0; }
BaseType_t xSemaphoreGive(SemaphoreHandle_t s) { return pthread_mutex_unlock(s) == 0; }

/* Event-Gruppe */
typedef struct { pthread_mutex_t m; pthread_cond_t c; EventBits_t bits; } evg_t;
EventGroupHandle_t xEventGroupCreate(void) { evg_t *e = calloc(1, sizeof *e); pthread_mutex_init(&e->m, NULL); pthread_cond_init(&e->c, NULL); return e; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t h, EventBits_t b) { evg_t *e = h; pthread_mutex_lock(&e->m); e->bits |= b; EventBits_t r = e->bits; pthread_cond_broadcast(&e->c); pthread_mutex_unlock(&e->m); return r; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t h, EventBits_t b, BaseType_t clear, BaseType_t all, TickType_t t)
{
    (void)all; (void)t;   /* nur mit portMAX_DELAY benutzt */
    evg_t *e = h; pthread_mutex_lock(&e->m);
    while (!(e->bits & b)) pthread_cond_wait(&e->c, &e->m);
    EventBits_t r = e->bits;
    if (clear) e->bits &= ~b;
    pthread_mutex_unlock(&e->m);
    return r;
}

/* Stream-Buffer (nicht blockierend, Teilschreiben wie FreeRTOS) */
typedef struct { pthread_mutex_t m; uint8_t *buf; size_t size, rd, used; } sb_t;
StreamBufferHandle_t xStreamBufferCreate(size_t size, size_t trig) { (void)trig; sb_t *s = calloc(1, sizeof *s); pthread_mutex_init(&s->m, NULL); s->buf = malloc(size); s->size = size; return s; }
void vStreamBufferDelete(StreamBufferHandle_t h) { sb_t *s = h; free(s->buf); free(s); }
size_t xStreamBufferSend(StreamBufferHandle_t h, const void *d, size_t n, TickType_t t)
{
    (void)t; sb_t *s = h; pthread_mutex_lock(&s->m);
    size_t k = s->size - s->used; if (k > n) k = n;
    for (size_t i = 0; i < k; i++) s->buf[(s->rd + s->used + i) % s->size] = ((const uint8_t *)d)[i];
    s->used += k; pthread_mutex_unlock(&s->m); return k;
}
size_t xStreamBufferReceive(StreamBufferHandle_t h, void *d, size_t n, TickType_t t)
{
    (void)t; sb_t *s = h; pthread_mutex_lock(&s->m);
    size_t k = s->used < n ? s->used : n;
    for (size_t i = 0; i < k; i++) ((uint8_t *)d)[i] = s->buf[(s->rd + i) % s->size];
    s->rd = (s->rd + k) % s->size; s->used -= k; pthread_mutex_unlock(&s->m); return k;
}
size_t xStreamBufferBytesAvailable(StreamBufferHandle_t h) { sb_t *s = h; pthread_mutex_lock(&s->m); size_t k = s->used; pthread_mutex_unlock(&s->m); return k; }
