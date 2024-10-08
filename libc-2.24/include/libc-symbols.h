/* Support macros for making weak and strong aliases for symbols,
   and for using symbol sets and linker warnings with GNU ld.
   Copyright (C) 1995-2016 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <http://www.gnu.org/licenses/>.  */

#ifndef _LIBC_SYMBOLS_H
#define _LIBC_SYMBOLS_H	1

#define IN_MODULE PASTE_NAME (MODULE_, MODULE_NAME)
#define IS_IN(lib) (IN_MODULE == MODULE_##lib)

/* Returns true if the current module is a versioned library.  Versioned
   library names culled from shlib-versions files are assigned a MODULE_*
   value lower than MODULE_LIBS_BEGIN.  */
#define IS_IN_LIB (IN_MODULE > MODULE_LIBS_BEGIN)

#define PASTE_NAME(a,b)      PASTE_NAME1 (a,b)
#define PASTE_NAME1(a,b)     a##b

/* This file's macros are included implicitly in the compilation of every
   file in the C library by -imacros.

   We include config.h which is generated by configure.
   It should define for us the following symbol:

   * HAVE_ASM_SET_DIRECTIVE if we have `.set B, A' instead of `A = B'.

   */

/* This is defined for the compilation of all C library code.  features.h
   tests this to avoid inclusion of stubs.h while compiling the library,
   before stubs.h has been generated.  Some library code that is shared
   with other packages also tests this symbol to see if it is being
   compiled as part of the C library.  We must define this before including
   config.h, because it makes some definitions conditional on whether libc
   itself is being compiled, or just some generator program.  */
#define _LIBC	1

/* Enable declarations of GNU extensions, since we are compiling them.  */
#define _GNU_SOURCE	1
/* And we also need the data for the reentrant functions.  */
#define _REENTRANT	1

#include "config.h"

/* Define this for the benefit of portable GNU code that wants to check it.
   Code that checks with #if will not #include <config.h> again, since we've
   already done it (and this file is implicitly included in every compile,
   via -include).  Code that checks with #ifdef will #include <config.h>,
   but that file should always be idempotent (i.e., it's just #define/#undef
   and nothing else anywhere should be changing the macro state it touches),
   so it's harmless.  */
#define HAVE_CONFIG_H	0

/* Define these macros for the benefit of portable GNU code that wants to check
   them.  Of course, STDC_HEADERS is never false when building libc!  */
#define STDC_HEADERS	1
#define HAVE_MBSTATE_T	1
#define HAVE_MBSRTOWCS	1
#define HAVE_LIBINTL_H	1
#define HAVE_WCTYPE_H	1
#define HAVE_ISWCTYPE	1
#define ENABLE_NLS	1

/* The symbols in all the user (non-_) macros are C symbols.  */

#ifndef __SYMBOL_PREFIX
# define __SYMBOL_PREFIX
#endif

#ifndef C_SYMBOL_NAME
# define C_SYMBOL_NAME(name) name
#endif

#ifndef ASM_LINE_SEP
# define ASM_LINE_SEP ;
#endif

#ifndef __ASSEMBLER__
/* GCC understands weak symbols and aliases; use its interface where
   possible, instead of embedded assembly language.  */

/* Define ALIASNAME as a strong alias for NAME.  */
# define strong_alias(name, aliasname) _strong_alias(name, aliasname)
# define _strong_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((alias (#name)));

/* This comes between the return type and function name in
   a function definition to make that definition weak.  */
# define weak_function __attribute__ ((weak))
# define weak_const_function __attribute__ ((weak, __const__))

/* Define ALIASNAME as a weak alias for NAME.
   If weak aliases are not available, this defines a strong alias.  */
# define weak_alias(name, aliasname) _weak_alias (name, aliasname)
# define _weak_alias(name, aliasname) \
  extern __typeof (name) aliasname __attribute__ ((weak, alias (#name)));

/* Same as WEAK_ALIAS, but mark symbol as hidden.  */
# define weak_hidden_alias(name, aliasname) \
  _weak_hidden_alias (name, aliasname)
# define _weak_hidden_alias(name, aliasname) \
  extern __typeof (name) aliasname \
    __attribute__ ((weak, alias (#name), __visibility__ ("hidden")));

/* Declare SYMBOL as weak undefined symbol (resolved to 0 if not defined).  */
# define weak_extern(symbol) _weak_extern (weak symbol)
# define _weak_extern(expr) _Pragma (#expr)


#else /* __ASSEMBLER__ */

# ifdef HAVE_ASM_SET_DIRECTIVE
#  define strong_alias(original, alias)				\
  .globl C_SYMBOL_NAME (alias) ASM_LINE_SEP		\
  .set C_SYMBOL_NAME (alias),C_SYMBOL_NAME (original)
#  define strong_data_alias(original, alias) strong_alias(original, alias)
# else
#  define strong_alias(original, alias)				\
  .globl C_SYMBOL_NAME (alias) ASM_LINE_SEP		\
  C_SYMBOL_NAME (alias) = C_SYMBOL_NAME (original)
#  define strong_data_alias(original, alias) strong_alias(original, alias)
# endif

# define weak_alias(original, alias)					\
  .weak C_SYMBOL_NAME (alias) ASM_LINE_SEP				\
  C_SYMBOL_NAME (alias) = C_SYMBOL_NAME (original)

# define weak_extern(symbol)						\
  .weak C_SYMBOL_NAME (symbol)

#endif /* __ASSEMBLER__ */

/* On some platforms we can make internal function calls (i.e., calls of
   functions not exported) a bit faster by using a different calling
   convention.  */
#ifndef internal_function
# define internal_function	/* empty */
#endif

/* Determine the return address.  */
#define RETURN_ADDRESS(nr) \
  __builtin_extract_return_addr (__builtin_return_address (nr))

/* When a reference to SYMBOL is encountered, the linker will emit a
   warning message MSG.  */
/* We want the .gnu.warning.SYMBOL section to be unallocated.  */
#define __make_section_unallocated(section_string)	\
  asm (".section " section_string "\n\t.previous");

/* Tacking on "\n\t#" to the section name makes gcc put it's bogus
   section attributes on what looks like a comment to the assembler.  */
#ifdef HAVE_SECTION_QUOTES
# define __sec_comment "\"\n\t#\""
#else
# define __sec_comment "\n\t#"
#endif
#define link_warning(symbol, msg) \
  __make_section_unallocated (".gnu.warning." #symbol) \
  static const char __evoke_link_warning_##symbol[]	\
    __attribute__ ((used, section (".gnu.warning." #symbol __sec_comment))) \
    = msg;
#define libc_freeres_ptr(decl) \
  __make_section_unallocated ("__libc_freeres_ptrs, \"aw\", %nobits") \
  decl __attribute__ ((section ("__libc_freeres_ptrs" __sec_comment)))
#define __libc_freeres_fn_section \
  __attribute__ ((section ("__libc_freeres_fn")))

#define libc_freeres_fn(name)	\
  static void name (void) __attribute_used__ __libc_freeres_fn_section;	\
  text_set_element (__libc_subfreeres, name);				\
  static void name (void)

/* A canned warning for sysdeps/stub functions.  */
#define	stub_warning(name) \
  __make_section_unallocated (".gnu.glibc-stub." #name) \
  link_warning (name, #name " is not implemented and will always fail")

/* Warning for linking functions calling dlopen into static binaries.  */
#ifdef SHARED
#define static_link_warning(name)
#else
#define static_link_warning(name) static_link_warning1(name)
#define static_link_warning1(name) \
  link_warning(name, "Using '" #name "' in statically linked applications \
requires at runtime the shared libraries from the glibc version used \
for linking")
#endif

/* Declare SYMBOL to be TYPE (`function' or `object') of SIZE bytes
   alias to ORIGINAL, when the assembler supports such declarations
   (such as in ELF).
   This is only necessary when defining something in assembly, or playing
   funny alias games where the size should be other than what the compiler
   thinks it is.  */
#define declare_symbol_alias(symbol, original, type, size) \
  declare_symbol_alias_1 (symbol, original, type, size)
#ifdef __ASSEMBLER__
# define declare_symbol_alias_1(symbol, original, type, size) \
   strong_alias (original, symbol); \
   .type C_SYMBOL_NAME (symbol), %##type; \
   .size C_SYMBOL_NAME (symbol), size
#else /* Not __ASSEMBLER__.  */
# define declare_symbol_alias_1(symbol, original, type, size) \
   asm (".globl " __SYMBOL_PREFIX #symbol \
	"\n\t" declare_symbol_alias_1_alias (symbol, original) \
	"\n\t.type " __SYMBOL_PREFIX #symbol ", " \
	"%" #type \
	"\n\t.size " __SYMBOL_PREFIX #symbol ", " #size);
# ifdef HAVE_ASM_SET_DIRECTIVE
#  define declare_symbol_alias_1_alias(symbol, original) \
     ".set " __SYMBOL_PREFIX #symbol ", " __SYMBOL_PREFIX #original
# else
#  define declare_symbol_alias_1_alias(symbol, original) \
     __SYMBOL_PREFIX #symbol " = " __SYMBOL_PREFIX #original
# endif /* HAVE_ASM_SET_DIRECTIVE */
#endif /* __ASSEMBLER__ */


/*

*/

/* Symbol set support macros.  */

/* Make SYMBOL, which is in the text segment, an element of SET.  */
#define text_set_element(set, symbol)	_elf_set_element(set, symbol)
/* Make SYMBOL, which is in the data segment, an element of SET.  */
#define data_set_element(set, symbol)	_elf_set_element(set, symbol)
/* Make SYMBOL, which is in the bss segment, an element of SET.  */
#define bss_set_element(set, symbol)	_elf_set_element(set, symbol)

/* These are all done the same way in ELF.
   There is a new section created for each set.  */
#ifdef SHARED
/* When building a shared library, make the set section writable,
   because it will need to be relocated at run time anyway.  */
# define _elf_set_element(set, symbol) \
  static const void *__elf_set_##set##_element_##symbol##__ \
    __attribute__ ((used, section (#set))) = &(symbol)
#else
# define _elf_set_element(set, symbol) \
  static const void *const __elf_set_##set##_element_##symbol##__ \
    __attribute__ ((used, section (#set))) = &(symbol)
#endif

/* Define SET as a symbol set.  This may be required (it is in a.out) to
   be able to use the set's contents.  */
#define symbol_set_define(set)	symbol_set_declare(set)

/* Declare SET for use in this module, if defined in another module.
   In a shared library, this is always local to that shared object.
   For static linking, the set might be wholly absent and so we use
   weak references.  */
#define symbol_set_declare(set) \
  extern char const __start_##set[] __symbol_set_attribute; \
  extern char const __stop_##set[] __symbol_set_attribute;
#ifdef SHARED
# define __symbol_set_attribute attribute_hidden
#else
# define __symbol_set_attribute __attribute__ ((weak))
#endif

/* Return a pointer (void *const *) to the first element of SET.  */
#define symbol_set_first_element(set)	((void *const *) (&__start_##set))

/* Return true iff PTR (a void *const *) has been incremented
   past the last element in SET.  */
#define symbol_set_end_p(set, ptr) ((ptr) >= (void *const *) &__stop_##set)

#ifdef SHARED
# define symbol_version(real, name, version) \
     _symbol_version(real, name, version)
# define default_symbol_version(real, name, version) \
     _default_symbol_version(real, name, version)
# ifdef __ASSEMBLER__
#  define _symbol_version(real, name, version) \
     .symver real, name##@##version
#  define _default_symbol_version(real, name, version) \
     .symver real, name##@##@##version
# else
#  define _symbol_version(real, name, version) \
     __asm__ (".symver " #real "," #name "@" #version)
#  define _default_symbol_version(real, name, version) \
     __asm__ (".symver " #real "," #name "@@" #version)
# endif
#else
# define symbol_version(real, name, version)
# define default_symbol_version(real, name, version) \
  strong_alias(real, name)
#endif

#if defined SHARED || defined LIBC_NONSHARED
# define attribute_hidden __attribute__ ((visibility ("hidden")))
#else
# define attribute_hidden
#endif

#define attribute_tls_model_ie __attribute__ ((tls_model ("initial-exec")))

#define attribute_relro __attribute__ ((section (".data.rel.ro")))

/* The following macros are used for PLT bypassing within libc.so
   (and if needed other libraries similarly).
   First of all, you need to have the function prototyped somewhere,
   say in foo/foo.h:

   int foo (int __bar);

   If calls to foo within libc.so should always go to foo defined in libc.so,
   then in include/foo.h you add:

   libc_hidden_proto (foo)

   line and after the foo function definition:

   int foo (int __bar)
   {
     return __bar;
   }
   libc_hidden_def (foo)

   or

   int foo (int __bar)
   {
     return __bar;
   }
   libc_hidden_weak (foo)

   Similarly for global data.  If references to foo within libc.so should
   always go to foo defined in libc.so, then in include/foo.h you add:

   libc_hidden_proto (foo)

   line and after foo's definition:

   int foo = INITIAL_FOO_VALUE;
   libc_hidden_data_def (foo)

   or

   int foo = INITIAL_FOO_VALUE;
   libc_hidden_data_weak (foo)

   If foo is normally just an alias (strong or weak) to some other function,
   you should use the normal strong_alias first, then add libc_hidden_def
   or libc_hidden_weak:

   int baz (int __bar)
   {
     return __bar;
   }
   strong_alias (baz, foo)
   libc_hidden_weak (foo)

   If the function should be internal to multiple objects, say ld.so and
   libc.so, the best way is to use:

   #if IS_IN (libc) || IS_IN (rtld)
   hidden_proto (foo)
   #endif

   in include/foo.h and the normal macros at all function definitions
   depending on what DSO they belong to.

   If versioned_symbol macro is used to define foo,
   libc_hidden_ver macro should be used, as in:

   int __real_foo (int __bar)
   {
     return __bar;
   }
   versioned_symbol (libc, __real_foo, foo, GLIBC_2_1);
   libc_hidden_ver (__real_foo, foo)  */

#if defined SHARED && !defined NO_HIDDEN
# ifndef __ASSEMBLER__
#  define __hidden_proto_hiddenattr(attrs...) \
  __attribute__ ((visibility ("hidden"), ##attrs))
#  define hidden_proto(name, attrs...) \
  __hidden_proto (name, , __GI_##name, ##attrs)
#  define hidden_tls_proto(name, attrs...) \
  __hidden_proto (name, __thread, __GI_##name, ##attrs)
#  define __hidden_proto(name, thread, internal, attrs...)	     \
  extern thread __typeof (name) name __asm__ (__hidden_asmname (#internal)) \
  __hidden_proto_hiddenattr (attrs);
#  define __hidden_asmname(name) \
  __hidden_asmname1 (__USER_LABEL_PREFIX__, name)
#  define __hidden_asmname1(prefix, name) __hidden_asmname2(prefix, name)
#  define __hidden_asmname2(prefix, name) #prefix name
#  define __hidden_ver1(local, internal, name) \
  extern __typeof (name) __EI_##name __asm__(__hidden_asmname (#internal)); \
  extern __typeof (name) __EI_##name \
	__attribute__((alias (__hidden_asmname (#local))))
#  define hidden_ver(local, name)	__hidden_ver1(local, __GI_##name, name);
#  define hidden_data_ver(local, name)	hidden_ver(local, name)
#  define hidden_def(name)		__hidden_ver1(__GI_##name, name, name);
#  define hidden_data_def(name)		hidden_def(name)
#  define hidden_weak(name) \
	__hidden_ver1(__GI_##name, name, name) __attribute__((weak));
#  define hidden_data_weak(name)	hidden_weak(name)
#  define hidden_nolink(name, lib, version) \
  __hidden_nolink1 (__GI_##name, __EI_##name, name, VERSION_##lib##_##version)
#  define __hidden_nolink1(local, internal, name, version) \
  __hidden_nolink2 (local, internal, name, version)
#  define __hidden_nolink2(local, internal, name, version) \
  extern __typeof (name) internal __attribute__ ((alias (#local))); \
  __hidden_nolink3 (local, internal, #name "@" #version)
#  define __hidden_nolink3(local, internal, vername) \
  __asm__ (".symver " #internal ", " vername);
# else
/* For assembly, we need to do the opposite of what we do in C:
   in assembly gcc __REDIRECT stuff is not in place, so functions
   are defined by its normal name and we need to create the
   __GI_* alias to it, in C __REDIRECT causes the function definition
   to use __GI_* name and we need to add alias to the real name.
   There is no reason to use hidden_weak over hidden_def in assembly,
   but we provide it for consistency with the C usage.
   hidden_proto doesn't make sense for assembly but the equivalent
   is to call via the HIDDEN_JUMPTARGET macro instead of JUMPTARGET.  */
#  define hidden_def(name)	strong_alias (name, __GI_##name)
#  define hidden_weak(name)	hidden_def (name)
#  define hidden_ver(local, name) strong_alias (local, __GI_##name)
#  define hidden_data_def(name)	strong_data_alias (name, __GI_##name)
#  define hidden_data_weak(name)	hidden_data_def (name)
#  define hidden_data_ver(local, name) strong_data_alias (local, __GI_##name)
#  define HIDDEN_JUMPTARGET(name) __GI_##name
# endif
#else
# ifndef __ASSEMBLER__
#  define hidden_proto(name, attrs...)
#  define hidden_tls_proto(name, attrs...)
# else
#  define HIDDEN_JUMPTARGET(name) JUMPTARGET(name)
# endif /* Not  __ASSEMBLER__ */
# define hidden_weak(name)
# define hidden_def(name)
# define hidden_ver(local, name)
# define hidden_data_weak(name)
# define hidden_data_def(name)
# define hidden_data_ver(local, name)
# define hidden_nolink(name, lib, version)
#endif

#if IS_IN (libc)
# define libc_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libc_hidden_tls_proto(name, attrs...) hidden_tls_proto (name, ##attrs)
# define libc_hidden_def(name) hidden_def (name)
# define libc_hidden_weak(name) hidden_weak (name)
# ifdef LINK_OBSOLETE_RPC
   /* libc_hidden_nolink_sunrpc should only get used in sunrpc code.  */
#  define libc_hidden_nolink_sunrpc(name, version) hidden_def (name)
# else
#  define libc_hidden_nolink_sunrpc(name, version) hidden_nolink (name, libc, version)
# endif
# define libc_hidden_ver(local, name) hidden_ver (local, name)
# define libc_hidden_data_def(name) hidden_data_def (name)
# define libc_hidden_data_weak(name) hidden_data_weak (name)
# define libc_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libc_hidden_proto(name, attrs...)
# define libc_hidden_tls_proto(name, attrs...)
# define libc_hidden_def(name)
# define libc_hidden_weak(name)
# define libc_hidden_ver(local, name)
# define libc_hidden_data_def(name)
# define libc_hidden_data_weak(name)
# define libc_hidden_data_ver(local, name)
#endif

#if IS_IN (rtld)
# define rtld_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define rtld_hidden_tls_proto(name, attrs...) hidden_tls_proto (name, ##attrs)
# define rtld_hidden_def(name) hidden_def (name)
# define rtld_hidden_weak(name) hidden_weak (name)
# define rtld_hidden_ver(local, name) hidden_ver (local, name)
# define rtld_hidden_data_def(name) hidden_data_def (name)
# define rtld_hidden_data_weak(name) hidden_data_weak (name)
# define rtld_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define rtld_hidden_proto(name, attrs...)
# define rtld_hidden_tls_proto(name, attrs...)
# define rtld_hidden_def(name)
# define rtld_hidden_weak(name)
# define rtld_hidden_ver(local, name)
# define rtld_hidden_data_def(name)
# define rtld_hidden_data_weak(name)
# define rtld_hidden_data_ver(local, name)
#endif

#if IS_IN (libm)
# define libm_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libm_hidden_tls_proto(name, attrs...) hidden_tls_proto (name, ##attrs)
# define libm_hidden_def(name) hidden_def (name)
# define libm_hidden_weak(name) hidden_weak (name)
# define libm_hidden_ver(local, name) hidden_ver (local, name)
# define libm_hidden_data_def(name) hidden_data_def (name)
# define libm_hidden_data_weak(name) hidden_data_weak (name)
# define libm_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libm_hidden_proto(name, attrs...)
# define libm_hidden_tls_proto(name, attrs...)
# define libm_hidden_def(name)
# define libm_hidden_weak(name)
# define libm_hidden_ver(local, name)
# define libm_hidden_data_def(name)
# define libm_hidden_data_weak(name)
# define libm_hidden_data_ver(local, name)
#endif

#if IS_IN (libmvec)
# define libmvec_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libmvec_hidden_tls_proto(name, attrs...) hidden_tls_proto (name, ##attrs)
# define libmvec_hidden_def(name) hidden_def (name)
# define libmvec_hidden_weak(name) hidden_weak (name)
# define libmvec_hidden_ver(local, name) hidden_ver (local, name)
# define libmvec_hidden_data_def(name) hidden_data_def (name)
# define libmvec_hidden_data_weak(name) hidden_data_weak (name)
# define libmvec_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libmvec_hidden_proto(name, attrs...)
# define libmvec_hidden_tls_proto(name, attrs...)
# define libmvec_hidden_def(name)
# define libmvec_hidden_weak(name)
# define libmvec_hidden_ver(local, name)
# define libmvec_hidden_data_def(name)
# define libmvec_hidden_data_weak(name)
# define libmvec_hidden_data_ver(local, name)
#endif

#if IS_IN (libresolv)
# define libresolv_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libresolv_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libresolv_hidden_def(name) hidden_def (name)
# define libresolv_hidden_weak(name) hidden_weak (name)
# define libresolv_hidden_ver(local, name) hidden_ver (local, name)
# define libresolv_hidden_data_def(name) hidden_data_def (name)
# define libresolv_hidden_data_weak(name) hidden_data_weak (name)
# define libresolv_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libresolv_hidden_proto(name, attrs...)
# define libresolv_hidden_tls_proto(name, attrs...)
# define libresolv_hidden_def(name)
# define libresolv_hidden_weak(name)
# define libresolv_hidden_ver(local, name)
# define libresolv_hidden_data_def(name)
# define libresolv_hidden_data_weak(name)
# define libresolv_hidden_data_ver(local, name)
#endif

#if IS_IN (librt)
# define librt_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define librt_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define librt_hidden_def(name) hidden_def (name)
# define librt_hidden_weak(name) hidden_weak (name)
# define librt_hidden_ver(local, name) hidden_ver (local, name)
# define librt_hidden_data_def(name) hidden_data_def (name)
# define librt_hidden_data_weak(name) hidden_data_weak (name)
# define librt_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define librt_hidden_proto(name, attrs...)
# define librt_hidden_tls_proto(name, attrs...)
# define librt_hidden_def(name)
# define librt_hidden_weak(name)
# define librt_hidden_ver(local, name)
# define librt_hidden_data_def(name)
# define librt_hidden_data_weak(name)
# define librt_hidden_data_ver(local, name)
#endif

#if IS_IN (libdl)
# define libdl_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libdl_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libdl_hidden_def(name) hidden_def (name)
# define libdl_hidden_weak(name) hidden_weak (name)
# define libdl_hidden_ver(local, name) hidden_ver (local, name)
# define libdl_hidden_data_def(name) hidden_data_def (name)
# define libdl_hidden_data_weak(name) hidden_data_weak (name)
# define libdl_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libdl_hidden_proto(name, attrs...)
# define libdl_hidden_tls_proto(name, attrs...)
# define libdl_hidden_def(name)
# define libdl_hidden_weak(name)
# define libdl_hidden_ver(local, name)
# define libdl_hidden_data_def(name)
# define libdl_hidden_data_weak(name)
# define libdl_hidden_data_ver(local, name)
#endif

#if IS_IN (libnss_files)
# define libnss_files_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libnss_files_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libnss_files_hidden_def(name) hidden_def (name)
# define libnss_files_hidden_weak(name) hidden_weak (name)
# define libnss_files_hidden_ver(local, name) hidden_ver (local, name)
# define libnss_files_hidden_data_def(name) hidden_data_def (name)
# define libnss_files_hidden_data_weak(name) hidden_data_weak (name)
# define libnss_files_hidden_data_ver(local, name) hidden_data_ver(local, name)
#else
# define libnss_files_hidden_proto(name, attrs...)
# define libnss_files_hidden_tls_proto(name, attrs...)
# define libnss_files_hidden_def(name)
# define libnss_files_hidden_weak(name)
# define libnss_files_hidden_ver(local, name)
# define libnss_files_hidden_data_def(name)
# define libnss_files_hidden_data_weak(name)
# define libnss_files_hidden_data_ver(local, name)
#endif

#if IS_IN (libnsl)
# define libnsl_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libnsl_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libnsl_hidden_def(name) hidden_def (name)
# define libnsl_hidden_weak(name) hidden_weak (name)
# define libnsl_hidden_ver(local, name) hidden_ver (local, name)
# define libnsl_hidden_data_def(name) hidden_data_def (name)
# define libnsl_hidden_data_weak(name) hidden_data_weak (name)
# define libnsl_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libnsl_hidden_proto(name, attrs...)
# define libnsl_hidden_tls_proto(name, attrs...)
# define libnsl_hidden_def(name)
# define libnsl_hidden_weak(name)
# define libnsl_hidden_ver(local, name)
# define libnsl_hidden_data_def(name)
# define libnsl_hidden_data_weak(name)
# define libnsl_hidden_data_ver(local, name)
#endif

#if IS_IN (libnss_nisplus)
# define libnss_nisplus_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libnss_nisplus_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libnss_nisplus_hidden_def(name) hidden_def (name)
# define libnss_nisplus_hidden_weak(name) hidden_weak (name)
# define libnss_nisplus_hidden_ver(local, name) hidden_ver (local, name)
# define libnss_nisplus_hidden_data_def(name) hidden_data_def (name)
# define libnss_nisplus_hidden_data_weak(name) hidden_data_weak (name)
# define libnss_nisplus_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libnss_nisplus_hidden_proto(name, attrs...)
# define libnss_nisplus_hidden_tls_proto(name, attrs...)
# define libnss_nisplus_hidden_def(name)
# define libnss_nisplus_hidden_weak(name)
# define libnss_nisplus_hidden_ver(local, name)
# define libnss_nisplus_hidden_data_def(name)
# define libnss_nisplus_hidden_data_weak(name)
# define libnss_nisplus_hidden_data_ver(local, name)
#endif

#define libc_hidden_builtin_proto(name, attrs...) libc_hidden_proto (name, ##attrs)
#define libc_hidden_builtin_def(name) libc_hidden_def (name)
#define libc_hidden_builtin_weak(name) libc_hidden_weak (name)
#define libc_hidden_builtin_ver(local, name) libc_hidden_ver (local, name)
#ifdef __ASSEMBLER__
# define HIDDEN_BUILTIN_JUMPTARGET(name) HIDDEN_JUMPTARGET(name)
#endif

#if IS_IN (libutil)
# define libutil_hidden_proto(name, attrs...) hidden_proto (name, ##attrs)
# define libutil_hidden_tls_proto(name, attrs...) \
  hidden_tls_proto (name, ##attrs)
# define libutil_hidden_def(name) hidden_def (name)
# define libutil_hidden_weak(name) hidden_weak (name)
# define libutil_hidden_ver(local, name) hidden_ver (local, name)
# define libutil_hidden_data_def(name) hidden_data_def (name)
# define libutil_hidden_data_weak(name) hidden_data_weak (name)
# define libutil_hidden_data_ver(local, name) hidden_data_ver (local, name)
#else
# define libutil_hidden_proto(name, attrs...)
# define libutil_hidden_tls_proto(name, attrs...)
# define libutil_hidden_def(name)
# define libutil_hidden_weak(name)
# define libutil_hidden_ver(local, name)
# define libutil_hidden_data_def(name)
# define libutil_hidden_data_weak(name)
# define libutil_hidden_data_ver(local, name)
#endif

/* Get some dirty hacks.  */
//#include <symbol-hacks.h>

/* Move compatibility symbols out of the way by placing them all in a
   special section.  */
#ifndef __ASSEMBLER__
# define attribute_compat_text_section \
    __attribute__ ((section (".text.compat")))
# define attribute_compat_data_section \
    __attribute__ ((section (".data.compat")))
#else
# define compat_text_section .section ".text.compat", "ax";
# define compat_data_section .section ".data.compat", "aw";
#endif

/* Marker used for indirection function symbols.  */
#define libc_ifunc(name, expr)						\
  extern void *name##_ifunc (void) __asm__ (#name);			\
  void *name##_ifunc (void)						\
  {									\
    INIT_ARCH ();							\
    __typeof (name) *res = expr;					\
    return res;								\
  }									\
  __asm__ (".type " #name ", %gnu_indirect_function");

/* The body of the function is supposed to use __get_cpu_features
   which will, if necessary, initialize the data first.  */
#define libm_ifunc(name, expr)						\
  extern void *name##_ifunc (void) __asm__ (#name);			\
  void *name##_ifunc (void)						\
  {									\
    __typeof (name) *res = expr;					\
    return res;								\
  }									\
  __asm__ (".type " #name ", %gnu_indirect_function");

#ifdef HAVE_ASM_SET_DIRECTIVE
# define libc_ifunc_hidden_def1(local, name)				\
    __asm__ (".globl " #local "\n\t"					\
	     ".hidden " #local "\n\t"					\
	     ".set " #local ", " #name);
#else
# define libc_ifunc_hidden_def1(local, name)				\
    __asm__ (".globl " #local "\n\t"					\
	     ".hidden " #local "\n\t"					\
	     #local " = " #name);
#endif

#define libc_ifunc_hidden_def(name) \
  libc_ifunc_hidden_def1 (__GI_##name, name)

/* Add the compiler optimization to inhibit loop transformation to library
   calls.  This is used to avoid recursive calls in memset and memmove
   default implementations.  */
#ifdef HAVE_CC_INHIBIT_LOOP_TO_LIBCALL
# define inhibit_loop_to_libcall \
    __attribute__ ((__optimize__ ("-fno-tree-loop-distribute-patterns")))
#else
# define inhibit_loop_to_libcall
#endif

#endif /* libc-symbols.h */
