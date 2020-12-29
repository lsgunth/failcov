// SPDX-License-Identifier: MIT
/* Copyright (C) 2020, Logan Gunthorpe */

#define _GNU_SOURCE

#define UNW_LOCAL_ONLY
#include <libunwind.h>

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

static volatile bool use_early_allocator;
static bool force_libc;

struct hash_entry {
	unsigned long long hash;
	struct hash_entry *next;
};

#define HASH_TABLE_SIZE 1024
#define HASH_TABLE_MASK (HASH_TABLE_SIZE - 1)
static pthread_mutex_t hash_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct hash_entry *callsite_table[HASH_TABLE_SIZE];

/*
 * Simple hash function based on
 *  http://www.cse.yorku.ca/~oz/hash.html
 */
#define HASH_INIT 53815381
static unsigned long long djb_hash(const char *inp, unsigned long long hash)
{
	while(*inp)
		hash = (hash * 33) ^ *inp++;

	return hash;
}

/*
 * Insert into a hash table, return 0 if the element already
 * exists, 1 if it was inserted.
 */
static int hash_table_insert(struct hash_entry *n, struct hash_entry **table)
{
	struct hash_entry **slot;
	int ret = 1;

	pthread_mutex_lock(&hash_table_mutex);

	slot = &table[n->hash & HASH_TABLE_MASK];

	while (*slot) {
		if ((*slot)->hash == n->hash) {
			ret = 0;
			goto out;
		}

		slot = &((*slot)->next);
	}

	*slot = n;

out:
	pthread_mutex_unlock(&hash_table_mutex);
	return ret;
}

static void __exit_error(const char *env, int err)
{
	const char *errstr = getenv(env);
	char *end;
	int tmp;

	if (errstr) {
		tmp = strtol(errstr, &end, 0);
		if (end != errstr && *end == '\0')
			err = tmp;
	}

	exit(err);
}

static void exit_error(void)
{
	__exit_error("FAILCOV_EXIT_ERROR", 32);
}

static struct hash_entry *create_hash_entry(void)
{
	struct hash_entry *h;

	h = malloc(sizeof(*h));
	if (!h) {
		perror("FAILCOV");
		exit_error();
	}

	h->next = NULL;
	h->hash = HASH_INIT;

	return h;
}

static FILE *load_database(void)
{
	const char *fname = getenv("FAILCOV_DATABASE");
	struct hash_entry *h;
	size_t read;
	FILE *dbf;
	int ret;

	if (!fname)
		fname = "failcov.db";

	dbf = fopen(fname, "a+b");
	if (!dbf) {
		fprintf(stderr, "FAILCOV: Unable to open '%s': %m\n", fname);
		exit_error();
	}

	h = create_hash_entry();

	while (1) {
		read = fread(&h->hash, sizeof(h->hash), 1, dbf);
		if (ferror(dbf)) {
			perror("FAILCOV: Unable to read database");
			exit_error();
		}

		if (read != 1)
			break;

		ret = hash_table_insert(h, callsite_table);
		if (ret)
			h = create_hash_entry();
	}

	free(h);
	return dbf;
}

static void write_callsite(FILE *dbf, struct hash_entry *h)
{
	size_t written;

	written = fwrite(&h->hash, sizeof(h->hash), 1, dbf);
	if (written != 1) {
		perror("FAILCOV: Unable to write database");
		exit_error();
	}

	/* flush in case the program crashes in the error handler */
	fflush(dbf);
}

static struct hash_entry *get_current_callsite(void)
{
	struct hash_entry *h = create_hash_entry();
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_word_t off;
	char name[4096];
	int ret;

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);

	while (unw_step(&cursor) > 0) {
		ret = unw_get_proc_name(&cursor, name, sizeof(name), &off);
		if (ret != 0)
			strcpy(name, "unknown");
		else
			snprintf(name + strlen(name),
				 sizeof(name) - strlen(name),
				 "+0x%lx", off);
		h->hash = djb_hash(name, h->hash);
	}

	return h;
}

static void print_backtrace(void)
{
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_word_t off;
	char name[4096];
	int ret;

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);

	/* Ignore the current function */
	unw_step(&cursor);

	while (unw_step(&cursor) > 0) {
		ret = unw_get_proc_name(&cursor, name, sizeof(name), &off);
		if (ret == 0)
			fprintf(stderr, "    %s+0x%lx\n", name, off);
		else
			fprintf(stderr, "    ?unknown\n");
	}
}

static void print_injection(void)
{
	fprintf(stderr, "FAILCOV: Injecting failure at:\n");
	print_backtrace();
}

static bool should_fail(const char *name)
{
	static FILE *dbf = NULL;
	static bool has_failed;
	struct hash_entry *h;
	bool ret = false;

	if (has_failed)
		return false;

	force_libc = true;

	if (!dbf)
		dbf = load_database();

	h = get_current_callsite();
	if (!h)
		goto out;

	ret = hash_table_insert(h, callsite_table);
	if (!ret) {
		free(h);
	} else {
		write_callsite(dbf, h);
		print_injection();
		has_failed = true;
	}

out:
	force_libc = false;
	return ret;
}

static void *early_allocator(size_t size)
{
	static char early_mem[4096];
	static int pos;

	if ((pos + size) > sizeof(early_mem))
		return NULL;

	pos += size;

	return &early_mem[pos - size];
}

#define call_super(name, ret_type, ...) ({ \
	static ret_type (*__super)(); \
	if (!__super) { \
		use_early_allocator = true; \
		__super = dlsym(RTLD_NEXT, #name); \
		use_early_allocator = false; \
	} \
	__super(__VA_ARGS__); \
})

#define handle_call(name, ret_type, err_ret, err_errno, ...) ({ \
	if (!force_libc && should_fail(#name)) { \
		errno = err_errno; \
		return err_ret; \
	}\
	call_super(name, ret_type, __VA_ARGS__); \
})

void *malloc(size_t size)
{
	if (use_early_allocator)
		return early_allocator(size);

	return handle_call(malloc, void *, NULL, ENOMEM, size);
}

void *calloc(size_t nmemb, size_t size)
{
	if (use_early_allocator)
		return early_allocator(nmemb * size);

	return handle_call(calloc, void *, NULL, ENOMEM, nmemb, size);
}

void *realloc(void *ptr, size_t size)
{
	return handle_call(realloc, void *, NULL, ENOMEM, ptr, size);
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
	return handle_call(reallocarray, void *, NULL, ENOMEM, ptr, nmemb,
			   size);
}

int creat(const char *pathname, mode_t mode)
{
	return handle_call(creat, int, -1, EACCES, pathname, mode);
}

int open(const char *pathname, int flags, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	return handle_call(open, int, -1, EACCES, pathname, flags, mode);
}

int openat(int dirfd, const char *pathname, int flags, ...)
{
	va_list ap;
	mode_t mode;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	return handle_call(openat, int, -1, EACCES, dirfd, pathname, flags,
			   mode);
}

ssize_t read(int fd, void *buf, size_t count)
{
	return handle_call(read, int, -1, EIO, fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return handle_call(write, int, -1, ENOSPC, fd, buf, count);
}
