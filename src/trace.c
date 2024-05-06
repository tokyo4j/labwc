#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sym_entry {
	uintptr_t addr;
	char *name;
};

static struct {
	FILE *stream;
	struct sym_entry *symbols;
	int sym_count;
	int level;
} profiler;

void __attribute__((no_instrument_function))
__cyg_profile_func_exit(void *func, void *caller);

void __attribute__((no_instrument_function))
__cyg_profile_func_enter(void *func, void *caller);

static int __attribute__((no_instrument_function))
compare_addr(const void *a, const void *b)
{
	const struct sym_entry *sym_a = a;
	const struct sym_entry *sym_b = b;
	return sym_a->addr - sym_b->addr;
}

static void __attribute__((no_instrument_function))
init_profiler(void)
{
	profiler.stream = fdopen(2, "w");
	if (profiler.stream == NULL)
		abort();
	setbuf(profiler.stream, NULL);

	int fd = open("/proc/self/exe", O_RDONLY);
	assert(fd > 0);

	elf_version(EV_CURRENT);
	Elf *elf = elf_begin(fd, ELF_C_READ, NULL);

	Elf_Scn *section = NULL;
	GElf_Shdr sym_header;
	while ((section = elf_nextscn(elf, section))) {
		gelf_getshdr(section, &sym_header);
		if (sym_header.sh_type == SHT_SYMTAB) {
			/* found a symbol table, go print it. */
			break;
		}
	}

	profiler.sym_count = sym_header.sh_size / sym_header.sh_entsize;
	profiler.symbols = malloc(sizeof(profiler.symbols[0]) * profiler.sym_count);

	unsigned long addr_diff = 0;
	Elf_Data *sym_table = elf_getdata(section, NULL);
	for (int i = 0; i < profiler.sym_count; i++) {
		GElf_Sym sym;
		gelf_getsym(sym_table, i, &sym);

		profiler.symbols[i].addr = sym.st_value;
		profiler.symbols[i].name = strdup(elf_strptr(
			elf, sym_header.sh_link, sym.st_name));

		const char *sym_name = elf_strptr(
			elf, sym_header.sh_link, sym.st_name);
		if (!strcmp(sym_name, "init_profiler")) {
			addr_diff = (uintptr_t)init_profiler - sym.st_value;
		}
	}
	assert(addr_diff);
	for (int i = 0; i < profiler.sym_count; i++) {
		profiler.symbols[i].addr += addr_diff;
	}
	qsort(profiler.symbols, profiler.sym_count, sizeof(profiler.symbols[0]), compare_addr);

	elf_end(elf);
	close(fd);
}


static char *__attribute__((no_instrument_function))
get_sym_name(uintptr_t addr)
{
	/* binary search */
	int l = 0;
	int r = profiler.sym_count - 1;
	while (l <= r) {
		int m = (l + r) / 2;
		uintptr_t sym_addr = profiler.symbols[m].addr;
		if (sym_addr == addr) {
			return profiler.symbols[m].name;
		} else if (sym_addr < addr) {
			l = m + 1;
		} else if (sym_addr > addr) {
			r = m - 1;
		}
	}
	return NULL;
}

void
__cyg_profile_func_enter(void *func, void *caller) {
	if (!profiler.symbols) {
		init_profiler();
	}
	for (int j = 0; j < profiler.level * 2; j++) {
		fputc(' ', profiler.stream);
	}
	fprintf(profiler.stream, "%s()\n", get_sym_name((uintptr_t)func));
	profiler.level++;
}

void
__cyg_profile_func_exit(void *func, void *caller) {
	profiler.level--;
}
