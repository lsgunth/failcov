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
#define TAG "\nFAILCOV: "

static volatile bool use_early_allocator;
static bool force_libc;
static bool found_bug;

struct hash_entry {
	unsigned long long hash;
	char *backtrace;
	struct hash_entry *next;
};

#define HASH_TABLE_SIZE 1024
#define HASH_TABLE_MASK (HASH_TABLE_SIZE - 1)
static pthread_mutex_t hash_table_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct hash_entry *callsite_table[HASH_TABLE_SIZE];
static struct hash_entry *allocation_table[HASH_TABLE_SIZE];
static struct hash_entry *fd_table[HASH_TABLE_SIZE];
static struct hash_entry *file_table[HASH_TABLE_SIZE];

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

static struct hash_entry *hash_table_pop(unsigned long long hash,
					 struct hash_entry **table)
{
	struct hash_entry **slot, *ret = NULL;

	pthread_mutex_lock(&hash_table_mutex);

	slot = &table[hash & HASH_TABLE_MASK];

	while (*slot) {
		if ((*slot)->hash == hash) {
			ret = *slot;
			*slot = ret->next;
			goto out;
		}

		slot = &((*slot)->next);
	}

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
	h->backtrace = NULL;
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
		fprintf(stderr, TAG "Unable to open '%s': %m\n", fname);
		exit_error();
	}

	h = create_hash_entry();

	while (1) {
		read = fread(&h->hash, sizeof(h->hash), 1, dbf);
		if (ferror(dbf)) {
			perror(TAG "Unable to read database");
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
	int ret;

	written = fwrite(&h->hash, sizeof(h->hash), 1, dbf);
	if (written != 1) {
		perror(TAG "Unable to write database");
		exit_error();
	}

	/* flush in case the program crashes in the error handler */
	ret = fflush(dbf);
	if (ret == EOF) {
		perror(TAG "Unable to write database");
		exit_error();
	}
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
	fprintf(stderr, TAG "Injecting failure at:\n");
	print_backtrace();
	fprintf(stderr, "\n");
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

static struct hash_entry *create_hash_entry_backtrace(void)
{
	char name[4096], backtrace[4096];
	struct hash_entry *h;
	unw_cursor_t cursor;
	unw_context_t uc;
	unw_word_t off;
	int boff = 0;
	int ret;

	unw_getcontext(&uc);
	unw_init_local(&cursor, &uc);

	while (unw_step(&cursor) > 0) {
		ret = unw_get_proc_name(&cursor, name, sizeof(name), &off);
		if (ret != 0)
			strcpy(name, "unknown");

		boff += snprintf(backtrace + boff, sizeof(backtrace) - boff,
				 "    %s+0x%lx\n", name, off);
	}

	h = create_hash_entry();
	h->backtrace = strdup(backtrace);

	return h;
}

static void track_create(unsigned long long hash,
			 struct hash_entry **table)
{
	struct hash_entry *h;

	if (force_libc)
		return;

	force_libc = true;

	h = create_hash_entry_backtrace();
	h->hash = hash;
	hash_table_insert(h, table);

	force_libc = false;
}

static void track_destroy(unsigned long long hash, struct hash_entry **table,
			  const char *msg)
{
	struct hash_entry *h;

	if (force_libc)
		return;

	force_libc = true;

	h = hash_table_pop(hash, table);
	if (!h) {
		fprintf(stderr, msg, hash);
		print_backtrace();
		found_bug = true;
	} else {
		if (h->backtrace)
			free(h->backtrace);
		free(h);
	}

	force_libc = false;
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
	void *ret;

	if (use_early_allocator)
		return early_allocator(size);

	ret = handle_call(malloc, void *, NULL, ENOMEM, size);
	if (ret)
		track_create((intptr_t)ret, allocation_table);

	return ret;
}

void *calloc(size_t nmemb, size_t size)
{
	void *ret;

	if (use_early_allocator)
		return early_allocator(nmemb * size);

	ret = handle_call(calloc, void *, NULL, ENOMEM, nmemb, size);
	if (ret)
		track_create((intptr_t)ret, allocation_table);

	return ret;
}

void *realloc(void *ptr, size_t size)
{
	void *ret;

	ret = handle_call(realloc, void *, NULL, ENOMEM, ptr, size);
	if (ret) {
		track_create((intptr_t)ret, allocation_table);
		track_destroy((intptr_t)ptr, allocation_table,
			      TAG "Attempted to realloc untracked pointer 0x%llx at:\n");
	}

	return ret;
}

void free(void *ptr)
{
	call_super(free, void, ptr);
	if (ptr)
		track_destroy((intptr_t)ptr, allocation_table,
			      TAG "Attempted to free untracked pointer 0x%llx at:\n");
}

void *reallocarray(void *ptr, size_t nmemb, size_t size)
{
	void *ret;

	ret = handle_call(reallocarray, void *, NULL, ENOMEM, ptr, nmemb,
			  size);
	if (ret) {
		track_create((intptr_t)ret, allocation_table);
		track_destroy((intptr_t)ptr, allocation_table,
			      TAG "Attempted to reallocarray untracked pointer 0x%llx at:\n");
	}

	return ret;
}

int creat(const char *pathname, mode_t mode)
{
	int fd;

	fd = handle_call(creat, int, -1, EACCES, pathname, mode);
	if (fd != -1)
		track_create(fd, fd_table);

	return fd;
}

int open(const char *pathname, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	fd = handle_call(open, int, -1, EACCES, pathname, flags, mode);
	if (fd != -1)
		track_create(fd, fd_table);

	return fd;
}

int openat(int dirfd, const char *pathname, int flags, ...)
{
	va_list ap;
	mode_t mode;
	int fd;

	va_start(ap, flags);
	mode = va_arg(ap, mode_t);
	va_end(ap);

	fd = handle_call(openat, int, -1, EACCES, dirfd, pathname, flags,
			 mode);
	if (fd != -1)
		track_create(fd, fd_table);

	return fd;
}

int close(int fd)
{
	track_destroy(fd, fd_table,
		      TAG "Attempted to close untracked file descriptor %lld at:\n");
	return handle_call(close, int, -1, EDQUOT, fd);
}

ssize_t read(int fd, void *buf, size_t count)
{
	return handle_call(read, int, -1, EIO, fd, buf, count);
}

ssize_t write(int fd, const void *buf, size_t count)
{
	return handle_call(write, int, -1, ENOSPC, fd, buf, count);
}

FILE *fopen(const char *pathname, const char *mode)
{
	FILE *f;

	f = handle_call(fopen, FILE *, NULL, EACCES, pathname, mode);
	if (f)
		track_create((intptr_t)f, file_table);

	return f;
}

FILE *fdopen(int fd, const char *mode)
{
	FILE *f;

	f = handle_call(fdopen, FILE *, NULL, EPERM, fd, mode);
	if (f) {
		track_create((intptr_t)f, file_table);
		track_destroy(fd, fd_table,
			      TAG "Attempted to fdopen untracked file descriptor %lld at:\n");
	}

	return f;
}

FILE *freopen(const char *pathname, const char *mode, FILE *stream)
{
	FILE *f;

	f = handle_call(fdopen, FILE *, NULL, EPERM, pathname, mode, stream);
	if (f) {
		track_create((intptr_t)f, file_table);
		track_destroy((intptr_t)stream, file_table,
			      TAG "Attempted to freopen untracked file 0x%llx at:\n");
	}

	return f;
}

FILE *fmemopen(void *buf, size_t size, const char *mode)
{
	FILE *f;

	f = handle_call(fmemopen, FILE *, NULL, ENOMEM, buf, size, mode);
	if (f)
		track_create((intptr_t)f, file_table);

	return f;
}

int fclose(FILE *stream)
{
	track_destroy((intptr_t)stream, file_table,
		      TAG "Attempted to fclose untracked file 0xllx at:\n");

	return handle_call(fclose, int, EOF, ENOSPC, stream);
}

int fcloseall(void)
{
	struct hash_entry *h;
	int i;

	for (i = 0; i < HASH_TABLE_SIZE; i++) {
		h = file_table[i];
		while (h) {
			if (h->backtrace)
				free(h->backtrace);
			free(h);

			h = h->next;
		}

		file_table[i] = NULL;
	}

	return handle_call(fcloseall, int, EOF, ENOSPC);
}

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	return handle_call(fwrite, size_t, 0, ENOSPC, ptr, size, nmemb,
			   stream);
}

/*
 * fread(): simulating an error for fread is not possible seeing the error
 * indicator cannot be set (see ferror()).
 */

int fflush(FILE *stream)
{
	return handle_call(fflush, int, EOF, ENOSPC, stream);
}

static bool should_ignore_leak(const char *backtrace)
{
	char *ignore = getenv("FAILCOV_LEAK_IGNORE");
	char *ignore_cpy, *tok;

	if (strstr(backtrace, "_IO_file_doallocate"))
		return true;

	if (!ignore)
		return false;

	ignore_cpy = strdup(ignore);
	if (!ignore_cpy)
		return false;

	tok = strtok(ignore, " ");
	while (tok) {
		if (strstr(backtrace, tok)) {
			free(ignore_cpy);
			return true;
		}

		tok = strtok(NULL, " ");
	}

	free(ignore_cpy);
	return false;
}

static void print_leak(struct hash_entry *h, const char *msg)
{
	found_bug = true;
	fprintf(stderr, msg, h->hash);

	if (h->backtrace)
		fprintf(stderr, "%s", h->backtrace);
	else
		fprintf(stderr, "unknown\n");
}

static void hdl_leaks(struct hash_entry *h, const char *msg)
{
	while (h) {
		if (!h->backtrace || !should_ignore_leak(h->backtrace))
			print_leak(h, msg);

		if (h->backtrace)
			free(h->backtrace);
		free(h);

		h = h->next;
	}
}

__attribute__((destructor))
static void check_leaks(void)
{
	int i;

	force_libc = true;

	for (i = 0; i < HASH_TABLE_SIZE; i++) {
		hdl_leaks(allocation_table[i],
			  TAG "Possible memory leak for 0x%llx allocated at:\n");
		hdl_leaks(fd_table[i],
			  TAG "Possible file descriptor leak for %lld opened at:\n");
		hdl_leaks(file_table[i],
			  TAG "Possible unclosed file for 0x%llx opened at:\n");
	}

	if (found_bug)
		__exit_error("FAILCOV_BUG_FOUND", 33);

	force_libc = false;
}
