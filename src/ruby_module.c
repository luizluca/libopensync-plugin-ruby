/*
 * file-sync - A plugin for the opensync framework
 * Copyright (C) 2004-2005  Armin Bauer <armin.bauer@opensync.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 *
 */

// TODO: study ruby memory managment and fix any memory leaks
// TODO Call free after any unregister. Maybe done
// DONE: all ruby calls must run inside a single thread (working on it)
// TODO: conversion is not working. opensync is still getting the data objtype.

#include "ruby_module.h"

#include <pthread.h>
#include <ruby/ruby.h>
#include <opensync/opensync-version.h>
#include <assert.h>
#include <stdlib.h>
#include <glib.h>
#include "opensyncRUBY_wrap.c"
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

#define RBOOL(value) ((value==Qfalse) || (value==Qnil) ? FALSE : TRUE)
#define BOOLR(value) (value==FALSE ? Qfalse : Qtrue)

#define IS_BOOL(value)   ((value==Qfalse) || (value==Qtrue))
/* Check_Type(val, T_STRING) raises exception but I these macros are
 * used in not-protected code */
#define IS_STRING(value) (TYPE(value)==T_STRING)
#define IS_ARRAY(value)  (TYPE(value)==T_ARRAY)
#define IS_FIXNUM(value) (FIXNUM_P(value))
#define IS_TIME(value)   (TYPE(value)==rb_cTime)

#define CAST_VALUE(value)     (value==NULL?Qnil:(VALUE)value)
#define IND_VALUE(value)      (value==NULL?Qnil:*(VALUE*)value)

#ifdef STACK_END_ADDRESS

/* Hack to run ruby from a pthread */
//https://github.com/whitequark/coldruby/blob/master/libcoldruby/MRIRubyCompiler.cpp
#include <sys/mman.h>
extern void *STACK_END_ADDRESS;
#define RUBY_PROLOGUE \
	do { \
		VALUE stack_dummy;\
		do {\
			void *stack_backup = STACK_END_ADDRESS; \
			STACK_END_ADDRESS = &stack_dummy;

#define RUBY_EPILOGUE \
			STACK_END_ADDRESS = stack_backup; \
		} while(0); \
	} while(0);
#else
#define RUBY_PROLOGUE
#define RUBY_EPILOGUE
#endif

/* Converts to string */
#define STR(args...)	#args

// #define DEBUG_MUTEX
// #define DEBUG_THREAD
#define DEBUG_FCALL

#ifdef DEBUG_MUTEX
#define pthread_mutex_lock(mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Locking " #mutex " at %s:%i\n",pthread_self(),__func__,__LINE__);pthread_mutex_lock(mutex);fprintf(stderr, "DEBUG_MUTEX[%lu]: Locked " #mutex " at %s:%i\n",pthread_self(),__func__,__LINE__);
#define pthread_mutex_unlock(mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Unocking " #mutex " at %s:%i\n",pthread_self(),__func__,__LINE__);pthread_mutex_unlock(mutex);
#define pthread_cond_wait(cond,mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Waiting " #cond " in " #mutex " at %s:%i\n",pthread_self(),__func__,__LINE__);pthread_cond_wait(cond,mutex);fprintf(stderr, "DEBUG_MUTEX[%lu]: Got " #cond " in " #mutex " at %s:%i\n",pthread_self(),__func__,__LINE__);
#define pthread_cond_signal(cond) fprintf(stderr, "DEBUG_MUTEX[%lu]: Signal " #cond " at %s:%i\n",pthread_self(),__func__,__LINE__);pthread_cond_signal(cond);
#define pthread_cond_broadcast(cond) fprintf(stderr, "DEBUG_MUTEX[%lu]: Broadcasting " #cond " at %s:%i\n",pthread_self(),__func__,__LINE__);pthread_cond_broadcast(cond);
#endif

#ifdef DEBUG_THREAD
#define debug_thread(format, args...) fprintf(stderr, "DEBUG_THREAD[%lu]:" format, pthread_self(), ## args)
#else
#define debug_thread(format, args...)
#endif

#ifdef DEBUG_FCALL
#define debug_fcall(format, args...) fprintf(stderr, "" format, ## args)
#else
#define debug_fcall(format, args...)
#endif

RUBY_GLOBAL_SETUP

/* This mutex avoids concurrent use of ruby context (which is prohibit) */
static pthread_mutex_t 	ruby_context_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t 	rubymodule_data_lock = PTHREAD_MUTEX_INITIALIZER;
//static pthread_t     	main_thread;
static pthread_t     	ruby_thread = 0;
static osync_bool      	ruby_running = FALSE;
static osync_bool      	ruby_started = FALSE;

pthread_cond_t 		fcall_ruby_running = PTHREAD_COND_INITIALIZER;
pthread_cond_t 		fcall_requested = PTHREAD_COND_INITIALIZER;
pthread_cond_t 		fcall_returned = PTHREAD_COND_INITIALIZER;
pthread_cond_t 		fcall_free = PTHREAD_COND_INITIALIZER;

static pthread_t 	ruby_thread;
static pthread_attr_t 	attr;

GHashTable 		*rubymodule_data;

void rubymodule_ruby_needed();

VALUE rb_funcall2_wrapper ( VALUE* params ) {
    VALUE result;
    pthread_t this_thread = pthread_self();
    char  *name;

    debug_fcall("STACK: %p ", &result);
    name = RSTRING_PTR(rb_funcall2 (params[0], rb_intern("inspect"), 0,0));
    debug_fcall("%s.%s()...",name, ( char* )params[1]);

    result = rb_funcall2 ( params[0], rb_intern ( ( char* ) params[1] ), ( int ) params[2], ( VALUE* ) params[3] );
    debug_fcall("returned!");

//     debug_fcall("GarbageCollecting...");
//     rb_funcall (rb_mGC, rb_intern ("start"), 0,  NULL);
//     debug_fcall("done!");

    debug_fcall("\n");
    return result;
}

static VALUE rb_funcall2_protected ( VALUE recv, const char* method, int argc, VALUE* args, int* status ) {
    VALUE params[4];
    VALUE result;
    int i;
    params[0]= recv;
    params[1]= ( VALUE ) method;
    params[2]= ( VALUE ) argc;
    params[3]= ( VALUE ) args;
    result=rb_protect(( VALUE ( * ) ( VALUE ) ) rb_funcall2_wrapper, ( VALUE ) params, status );
    return result;
}

static char * osync_rubymodule_error_bt ( char* file, const char* func, int line ) {
    VALUE message;
    int state;
    message = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_eval_string, ( VALUE ) ( "bt=$!.backtrace; bt[0]=\"#{bt[0]}: #{$!} (#{$!.class})\"; bt.join('\n')" ),&state );
    if ( state!=0 ) {
        return "Unable to obtain BT!";
    }
    return RSTRING_PTR ( message );
}

static void unregister_and_free(gpointer data) {
    rb_gc_unregister_address(data);
    free(data);
};

static void osync_rubymodule_set_data ( void* ptr, char const *key, VALUE data ) {
    GHashTable *ptr_data;
    VALUE saved_data;

    pthread_mutex_lock ( &rubymodule_data_lock );

    ptr_data = ( GHashTable* ) g_hash_table_lookup ( rubymodule_data, ptr );
    if ( ptr_data == NULL ) {
        ptr_data = g_hash_table_new_full ( &g_str_hash, &g_str_equal, NULL, &unregister_and_free );
        g_hash_table_insert ( rubymodule_data, ptr, ptr_data );
    }

    /* Free the value if present */
    g_hash_table_remove ( ptr_data, key );

    if ( data != Qnil ) {
        //rb_gc_register_address ( &data );
        VALUE *pdata = malloc(sizeof(VALUE));
        *pdata = data;
        g_hash_table_insert ( ptr_data, ( char* ) key, pdata );
        rb_gc_register_address ( pdata );
    }

    pthread_mutex_unlock ( &rubymodule_data_lock );
}

static VALUE osync_rubymodule_get_data ( void* ptr, char const *key ) {
    GHashTable *ptr_data;
    VALUE      *saved_data;

//     fprintf(stderr, "%p[\"%s\"@%p]-> ", ptr, key, &key);

    ptr_data = ( GHashTable* ) g_hash_table_lookup ( rubymodule_data, ptr );
    if ( ptr_data == NULL ) {
// 	fprintf(stderr, "no values\n");
        return Qnil;
    }

    saved_data = ( VALUE* ) g_hash_table_lookup ( ptr_data, key );
    if ( !saved_data ) {
// 	fprintf(stderr, "%s not found\n", key);
        return Qnil;
    }

//     fprintf(stderr, "%lu\n", ptr, key, saved_data);
    return *saved_data;
}

static void osync_rubymodule_clean_data ( void* ptr ) {
    g_hash_table_remove ( rubymodule_data, ptr );
}

static VALUE rb_osync_rubymodule_get_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    key = RSTRING_PTR ( argv[1] );
    vresult = osync_rubymodule_get_data ( ptr, key );
    return vresult;
fail:
    return Qnil;
}

static VALUE rb_osync_rubymodule_set_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 3 ) || ( argc > 3 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 3)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    key = RSTRING_PTR ( argv[1] );
    osync_rubymodule_set_data ( ptr, key, argv[2] );
    return Qnil;
fail:
    return Qnil;
}

static VALUE rb_osync_rubymodule_clean_data ( int argc, VALUE *argv, VALUE self ) {
    void *ptr = 0;
    char const *key;
    int res1 = 0 ;
    VALUE vresult = Qnil;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &ptr, 0 , 0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "void*", "osync_rubymodule_get_ptr_data", 1, argv[0] ) );
    }
    osync_rubymodule_clean_data ( ptr );
    return Qnil;
fail:
    return Qnil;
}

static void free_plugin_data ( VALUE *data ) {
    // I guess gc will free this data
    rb_gc_unregister_address ( data );
    free(data);
}

/* Converter */
static osync_bool osync_rubymodule_converter_convert (OSyncFormatConverter *conv,  char *input, unsigned int inpsize, char **output, unsigned int *outpsize, osync_bool *free_input, const char *config, void *user_data, OSyncError **error ) {
        int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( conv, "convert_func" );


    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( conv ), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( input, inpsize );
    args[2] = SWIG_FromCharPtr ( config );
    args[3] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call converter convert function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_ARRAY ( result ) || ( RARRAY_LEN ( result ) != 2 ) ||
            !IS_STRING ( rb_ary_entry ( result, 0 ) ) ||
            !IS_BOOL ( rb_ary_entry ( result, 1 ) )
       ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of print should be an Array with [output:string, free_input:bool] !\n" );
        goto error;
    }

    *output = RSTRING_PTR ( rb_ary_entry ( result, 0 ) );
    *outpsize = RSTRING_LEN ( rb_ary_entry ( result, 0 ) );
    *free_input  = RBOOL ( rb_ary_entry ( result, 1 ) );

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

VALUE rb_load_basefile(const char *filename) {
    return rb_require(RUBY_BASE_FILE);
}

VALUE rb_load_metaclass(const char *classpath) {
    rb_load_basefile(RUBY_BASE_FILE);
    return rb_path2class(classpath);
}

VALUE rb_get_sync_info(VALUE plugin_env) {
    VALUE meta_class = rb_load_metaclass(RUBY_PLUGIN_CLASS);
    return rb_funcall(meta_class,rb_intern("get_sync_info"), 1, plugin_env);
}

VALUE rb_get_conversion_info(VALUE format_env) {
    VALUE meta_class = rb_load_metaclass(RUBY_FORMAT_CLASS);
    return rb_funcall(meta_class,rb_intern("get_conversion_info"), 1, format_env);
}

VALUE rb_get_format_info(VALUE format_env) {
    VALUE meta_class = rb_load_metaclass(RUBY_FORMAT_CLASS);
    return rb_funcall(meta_class,rb_intern("get_format_info"), 1, format_env);
}

#include "callbacks.c"

/** Plugin */

VALUE rb_osync_plugin_set_data ( int argc, VALUE *argv, VALUE self ) {
    OSyncPlugin *arg1 = ( OSyncPlugin * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    VALUE *data = 0 ;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncPlugin *","osync_plugin_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncPlugin * ) ( argp1 );

    data = ( VALUE* ) osync_plugin_get_data ( arg1 );
    if ( data ) {
        rb_gc_unregister_address ( data );
	free(data);
    }
    data = NULL;
    if (argv[1]!=Qnil) {
	data = malloc(sizeof(VALUE));
	*data = argv[1];
	rb_gc_register_address ( data );
    }
    osync_plugin_set_data ( arg1, data );
    return Qnil;
fail:
    return Qnil;
}

/** ObjectType Sinks */
VALUE rb_osync_objtype_sink_get_userdata ( int argc, VALUE *argv, VALUE self ) {
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    VALUE *vresult = NULL;

    if ( ( argc < 1 ) || ( argc > 1 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 1)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_get_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    vresult = osync_objtype_sink_get_userdata ( arg1 );

    if ( vresult )
        return *vresult;
    return Qnil;
fail:
    return Qnil;
}

VALUE rb_osync_objtype_sink_set_userdata ( int argc, VALUE *argv, VALUE self ) {
    OSyncObjTypeSink *arg1 = ( OSyncObjTypeSink * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    VALUE *data;

    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "OSyncObjTypeSink *","osync_objtype_sink_set_data", 1, argv[0] ) );
    }
    arg1 = ( OSyncObjTypeSink * ) ( argp1 );
    data = osync_objtype_sink_get_userdata ( arg1 );
    if ( data ) {
        rb_gc_unregister_address ( data );
	free(data);
    }
    data = NULL;
    if (argv[1]!=Qnil) {
	data = malloc(sizeof(VALUE));
	*data = argv[1];
	rb_gc_register_address ( data );
    }
    osync_objtype_sink_set_userdata ( arg1, data);
    return Qnil;
fail:
    return Qnil;
}

/*
  Document-method: Opensync.osync_converter_new

  call-seq:
    osync_converter_new(OSyncConverterType type, OSyncObjFormat sourceformat,
    OSyncObjFormat targetformat, OSyncFormatConvertFunc convert_func,
    OSyncError error) -> OSyncFormatConverter

A module function.

*/
VALUE rb_osync_converter_new(int argc, VALUE *argv, VALUE self) {
  OSyncConverterType arg1 ;
  OSyncObjFormat *arg2 = (OSyncObjFormat *) 0 ;
  OSyncObjFormat *arg3 = (OSyncObjFormat *) 0 ;
  OSyncFormatConvertFunc arg4 = (OSyncFormatConvertFunc) 0 ;
  OSyncError **arg5 = (OSyncError **) 0 ;
  OSyncError *error5 ;
  int val1 ;
  int ecode1 = 0 ;
  void *argp2 = 0 ;
  int res2 = 0 ;
  void *argp3 = 0 ;
  int res3 = 0 ;
  void *argp5 = 0 ;
  int res5 = 0 ;
  OSyncFormatConverter *result = 0 ;
  VALUE vresult = Qnil;

  {
    error5 = NULL;
    arg5 = &error5;
  }
  if ((argc < 4) || (argc > 5)) {
    rb_raise(rb_eArgError, "wrong # of arguments(%d for 4)",argc); SWIG_fail;
  }
  ecode1 = SWIG_AsVal_int(argv[0], &val1);
  if (!SWIG_IsOK(ecode1)) {
    SWIG_exception_fail(SWIG_ArgError(ecode1), Ruby_Format_TypeError( "", "OSyncConverterType","osync_converter_new", 1, argv[0] ));
  }
  arg1 = (OSyncConverterType)(val1);
  res2 = SWIG_ConvertPtr(argv[1], &argp2,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
  if (!SWIG_IsOK(res2)) {
    SWIG_exception_fail(SWIG_ArgError(res2), Ruby_Format_TypeError( "", "OSyncObjFormat *","osync_converter_new", 2, argv[1] ));
  }
  arg2 = (OSyncObjFormat *)(argp2);
  res3 = SWIG_ConvertPtr(argv[2], &argp3,SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
  if (!SWIG_IsOK(res3)) {
    SWIG_exception_fail(SWIG_ArgError(res3), Ruby_Format_TypeError( "", "OSyncObjFormat *","osync_converter_new", 3, argv[2] ));
  }
  arg3 = (OSyncObjFormat *)(argp3);

//   {
//     int res = SWIG_ConvertFunctionPtr(argv[3], (void**)(&arg4), SWIGTYPE_p_f_p_char_unsigned_int_p_p_char_p_unsigned_int_p_int_p_q_const__char_p_void_p_p_struct_OSyncError__int);
//     if (!SWIG_IsOK(res)) {
//       SWIG_exception_fail(SWIG_ArgError(res), Ruby_Format_TypeError( "", "OSyncFormatConvertFunc","osync_converter_new", 4, argv[3] ));
//     }
//   }
  if (argc > 4) {
    res5 = SWIG_ConvertPtr(argv[4], &argp5,SWIGTYPE_p_p_OSyncError, 0 |  0 );
    if (!SWIG_IsOK(res5)) {
      SWIG_exception_fail(SWIG_ArgError(res5), Ruby_Format_TypeError( "", "OSyncError **","osync_converter_new", 5, argv[4] ));
    }
    arg5 = (OSyncError **)(argp5);
  }
  result = (OSyncFormatConverter *)osync_converter_new(arg1,arg2,arg3,osync_rubymodule_converter_convert,arg5);
  osync_rubymodule_set_data (result, "convert_func", argv[3] );
  vresult = SWIG_NewPointerObj(SWIG_as_voidptr(result), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 );
  {
    if (error5) {
      /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
      *     if (rb_eOSync == Qnil)
      *       rb_eOSync = rb_eStandardError;  */
      rb_raise(rb_eStandardError, "%s",osync_error_print(&error5));
      osync_error_unref(&error5);
      SWIG_fail;
    }
  }
  return vresult;
fail:
  {
    if (error5) {
      /*     VALUE rb_eOSync = rb_path2class("Opensync::Error");
      *     if (rb_eOSync == Qnil)
      *       rb_eOSync = rb_eStandardError;  */
      rb_raise(rb_eStandardError, "%s",osync_error_print(&error5));
      osync_error_unref(&error5);
      SWIG_fail;
    }
  }
  return Qnil;
}

/**
 * @brief This register ruby module and methods and initialize internal local data structure
 */
void rubymodule_initialize() {
    // Initialize SWIG methods
    Init_opensync();
    // Initialize callbacks methods
    Init_rubymodule_callbacks();
    // Expose some internal methods to ruby world
    rb_define_module_function ( mOpensync, "osync_rubymodule_get_data", rb_osync_rubymodule_get_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_set_data", rb_osync_rubymodule_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_clean_data", rb_osync_rubymodule_clean_data, -1 );
    // Converter new/set_callback implementation
    rb_define_module_function ( mOpensync, "osync_converter_new", rb_osync_converter_new, -1 );
    // Some constants exposed to RUBY
    rb_define_const(mOpensync, "OPENSYNC_RUBYPLUGIN_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYPLUGIN_DIR));
    rb_define_const(mOpensync, "OPENSYNC_RUBYFORMATS_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYFORMATS_DIR));
    // Initialize hash that maps objects to its properties (which include callbacks blocks)
    rubymodule_data = g_hash_table_new_full ( g_direct_hash, g_direct_equal, NULL, ( GDestroyNotify ) g_hash_table_destroy );
}

void rubymodule_finalize() {
    g_hash_table_destroy ( rubymodule_data );
    RUBY_PROLOGUE
    ruby_finalize();
    RUBY_EPILOGUE
}

osync_bool is_rubythread() {
  return ruby_thread == pthread_self();
}

void *rubymodule_ruby_thread(void *threadid) {
    debug_thread("Thread launched!\n");
    pthread_mutex_lock ( &ruby_context_lock);
    int page = sysconf(_SC_PAGE_SIZE);
    mprotect((void *)((unsigned long)&STACK_END_ADDRESS & ~(page - 1)), page, PROT_READ | PROT_WRITE | PROT_EXEC);
    RUBY_PROLOGUE
    RUBY_INIT_STACK;
    ruby_init();
    ruby_init_loadpath();
    ruby_script ( RUBY_SCRIPTNAME );
    rubymodule_initialize();
    RUBY_EPILOGUE
    debug_thread("Accepting commands!\n");
    ruby_running=TRUE;
    ruby_thread = pthread_self();
    pthread_cond_broadcast(&fcall_ruby_running);
    while (ruby_running) {
       debug_thread("Waiting a command!\n");
       pthread_cond_wait(&fcall_requested, &ruby_context_lock);
       debug_thread("Got command! Executing\n");
       RUBY_PROLOGUE
       funcall_data.func();
       RUBY_EPILOGUE
       debug_thread("Returning!\n");
       pthread_cond_signal(&fcall_returned);
    }
    rubymodule_finalize();
    pthread_mutex_unlock ( &ruby_context_lock);
    pthread_exit(0);
}

void rubymodule_ruby_needed() {
    if (ruby_started )
	return;
    pthread_mutex_lock ( &ruby_context_lock);
    if (! ruby_started ) {
       int rc;
       pthread_attr_t attr;
       pthread_attr_init(&attr);
       pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
       pthread_attr_setstacksize (&attr, 100*1000*1000);
       rc = pthread_create(&ruby_thread, NULL, rubymodule_ruby_thread, NULL);
       if (rc){
           fprintf(stderr,"ERROR; return code from pthread_create() is %d\n", rc);
           exit(-1);
       }
       pthread_attr_destroy(&attr);
       ruby_started=TRUE;
    }
    pthread_mutex_unlock ( &ruby_context_lock);
}

int get_version ( void ) {
    return 1;
}


/* Suggestions:
 *
* make all _new function behave the same: only malloc or malloc and
also define mandatory attributes.
* if _new only mallocs, create the necessary setters and getters for
each of the attributes
* add a set/get_data to objformat.
* Some callbacks returns values while others not. Why return
osync_bool if error is enough to tell if the function
worked? Why force initialization to return a void* if, maybe, the
initialization is not used to create a struct for data?
* Choose one unique data field name. I found three: user_data, userdata and data
PROPOSED* Plugin callback define methods in plugin
(set_initialize/finalize/discovery) does not have a suffix like in
sink and others. Maybe a osync_plugin_set_initialize_func would sound
better.
 * Is there any difference in osync_format and osync_objformat? I know
that there is only osync_format_env but this format isn't really
objformat?
* osync_objtype_main_sink_new and osync_objtype_sink_new
 */

// osync_caps_converter_new and osync_converter_new ????
// file-sync does not "osync_trace ( TRACE_EXIT, "%s: true", __func__);"
// BUG: Even if demarshal fails, it accepts the sync
// BUG: double random at 	file->path = osync_rand_str(g_random_int_range(1, 100), error);

