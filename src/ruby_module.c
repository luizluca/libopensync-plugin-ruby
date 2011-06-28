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

// TODO Call free after any unregister
// BUG: all ruby calls must run inside a single thread (working on it)

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

//#define DEBUG_MUTEX
//#define DEBUG_THREAD
#define DEBUG_FCALL

#ifdef DEBUG_MUTEX
#define pthread_mutex_lock(mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Locking " #mutex " at %i\n",pthread_self(),__LINE__);pthread_mutex_lock(mutex);
#define pthread_mutex_unlock(mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Unocking " #mutex " at %i\n",pthread_self(),__LINE__);pthread_mutex_unlock(mutex);
#define pthread_cond_wait(cond,mutex) fprintf(stderr, "DEBUG_MUTEX[%lu]: Waiting " #cond " in " #mutex " at %i\n",pthread_self(),__LINE__);pthread_cond_wait(cond,mutex);
#define pthread_cond_signal(cond) fprintf(stderr, "DEBUG_MUTEX[%lu]: Signal " #cond " at %i\n",pthread_self(),__LINE__);pthread_cond_signal(cond);
#define pthread_cond_broadcast(cond) fprintf(stderr, "DEBUG_MUTEX[%lu]: Broadcasting " #cond " at %i\n",pthread_self(),__LINE__);pthread_cond_broadcast(cond);
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
static pthread_t     	main_thread;
static osync_bool      	ruby_running = FALSE;
static osync_bool      	ruby_started = FALSE;
pthread_cond_t 		fcall_ruby_running = PTHREAD_COND_INITIALIZER;
pthread_cond_t 		fcall_requested = PTHREAD_COND_INITIALIZER;
pthread_cond_t 		fcall_returned = PTHREAD_COND_INITIALIZER;

static pthread_t 	ruby_thread;
static pthread_attr_t 	attr;

// struct funcall_args_t {
//    VALUE (*func)(VALUE);
//    VALUE arg;
//    int *error;
//    VALUE result;
// };
// #define FREE_FUNCALL_ARGS { NULL, Qnil, NULL, Qnil};
// static struct funcall_args_t funcall_args = FREE_FUNCALL_ARGS;

// static int      	ruby_uses = 0;
GHashTable 		*rubymodule_data;

void rubymodule_ruby_needed();

struct threaded_funcall;
typedef void* (* threaded_func) ();
struct arg_desc {
  char         *type;
  char         *name;
  void	       *ptr;
};
struct threaded_funcall {
   threaded_func 	func;
   int 			nargin;
   int 			nargout;
   struct arg_desc	*argsin;
   struct arg_desc	*argsout;
   OSyncError		**error;
   void   		*result;
};
static struct threaded_funcall funcall_data;

size_t strcount(const char *string, const char chr) {
  size_t i;
  size_t count=0;
  for (i=0; i<strlen(string); i++) {
     if (string[i]==chr)
       count++;
  }
  return count;
}

char* strdeblank(char *instr) {
  char *str = g_strchug(instr);
  char *result = str;
  size_t i,j, len;
  len=strlen(str);
  for (i=0,j=0; i<len; i++) {
      if (!isblank(str[i])) {
	result[j++]=str[i];
      }
  }
  result[j]='\0';
  return result;
}

void save_args(char* signature, char* strargs, int argc, struct arg_desc *args, ...) {
    int i;
    void* arg;
    va_list ap;

    char *_strargs = strdeblank(strdup(strargs));
    char *running_name = _strargs;
    char *token;

    va_start(ap, args);
    for (i = 0; i < argc; i++) {
	arg = va_arg(ap, void*);
	args[i].ptr=arg;
	args[i].name=strdup(strsep(&running_name,","));
	// TODO Error on empty

	char* _signature=strdup(signature);
	char* running_sig=_signature;
	args[i].type="void";
	while (token=strsep(&running_sig,",")){
	    token=g_strstrip(token);
	    if (!g_str_has_suffix(token, args[i].name)) {
	      continue;
	    }
	    char prev=token[strlen(token)-strlen(args[i].name)-1];
	    if (isalpha(prev) || (prev=='_')) {
	      continue;
	    }
	    // Matched, now clean up
	    token[strlen(token)-strlen(args[i].name)]='\0';
	    // Find the last alphanumeric
	    size_t pos;
	    for (pos=strlen(token)-1;pos>0;pos--) {
	      if (isalpha(token[pos])) {
		break;
	      }
	    }
	    // and unblank after it
	    strdeblank(token+pos);
	    args[i].type=strdup(token);
	    break;
	}
	free(_signature);
	//args[i].type=g_strstrip(g_strdup(strsep(&cur_arg,",")));
    }
    free(_strargs);
    va_end(ap);
}


#define CAST_AS_RUBY(ruby_arg, arg_desc) \
      swig_type_info *type = SWIG_TypeQuery(arg_desc.type);\
      if (type) { \
	ruby_args[i]=SWIG_NewPointerObj ( argsin[i].ptr, type, 0 |  0 );\
      } else { \
	ruby_args[i]=Qnil;\
      }

#define CAST_ALL_AS_RUBY(ruby_args, args, narg) \
    VALUE ruby_args[narg];\
    for (i=0; i<narg; i++) {\
      CAST_AS_RUBY(ruby_args[i], args[i]); \
    }

/* Generate a ruby method that, when called, will be executed inside the ruby thread
 * It uses some defines as parameters
 * #define rb_result_type 	The result type (i.e.: void*)
 * #define rb_module		The corresponding module name (i.e.: plugin)
 * #define rb_function		The function name (i.e.: initialize)
 * #define rb_sig      		This function signature (i.e.: OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error )
 * 				All functions must contain **error.
 * #define rb_argins  		The arguments used as input or 0 for nothing. (i.e.: plugin, info)
 * 				These will be accessible in code with (struct arg_desc *argsin) with size of nargin
 * #define rb_argouts  		The arguments used as ouput or 0 for nothing: (i.e.: 0)
 * 				These will be accessible in code with (struct arg_desc *argsout) with size of nargout
 *
 * It generates two methods:
 *
 *   NAME_RUBYCALL_FUNC(rb_module, rb_function)
 *   NAME_RUBYCALL_FUNC(rb_module, rb_function##_threaded)
 *
 * Which expands to:
 *
 * osync_rubymodule_rb_module_rb_function
 * osync_rubymodule_rb_module_rb_function_threaded
 *
 * The first one can be called normally. The second one is internal use only and it is called from the ruby thread.
 *
 * The code is written after BEGIN_RUBYCALL_FUNC and ends with END_RUBYCALL_FUNC. The result value must goes
 * inside result local variable. If some error occured, set the error variable and "goto error;"
 */

/* Generate a ruby func name */
#define NAME_RUBYCALL_FUNC(module, function)      osync_rubymodule_##module##_##function
#define _NAME_RUBYCALL_FUNC(module, function)         NAME_RUBYCALL_FUNC(module, function)
#define _NAME_RUBYCALL_FUNC_THREADED(module, function)         NAME_RUBYCALL_FUNC(module, function##_threaded)
#define _STR(X)		STR(X)
#define BEGIN_RUBYCALL_FUNC \
static void* _NAME_RUBYCALL_FUNC_THREADED(rb_module,rb_function)();\
static rb_result_type _NAME_RUBYCALL_FUNC(rb_module,rb_function)(rb_sig) { \
    char *signature  = strdup(_STR(rb_sig)); \
    char *str_argin  = strdeblank(strdup(_STR(rb_argins))); \
    char *str_argout  = strdeblank(strdup(_STR(rb_argouts))); \
    int nargin = str_argin[0]=='0'?   0 : strcount(str_argin, ',')+1; \
    int nargout = str_argout[0]=='0'? 0 : strcount(str_argout, ',')+1; \
    int i; \
    rb_result_type result; \
    struct arg_desc argsin[nargin]; \
    struct arg_desc argsout[nargout]; \
    rubymodule_ruby_needed(); \
    save_args(signature, str_argin,  nargin, argsin, rb_argins); \
    save_args(signature, str_argout, nargout, argsout, rb_argouts);\
    debug_thread("Locking!\n"); \
    pthread_mutex_lock ( &ruby_context_lock); \
    debug_thread("Waiting for my time!\n"); \
    if (!ruby_running) { \
	debug_thread("Ruby thread is not running. Waiting\n"); \
	pthread_cond_wait(&fcall_ruby_running, &ruby_context_lock); \
	debug_thread("Ruby thread is running!\n"); \
    } \
    funcall_data.nargin  = nargin; \
    funcall_data.nargout = nargout; \
    funcall_data.argsin  = argsin; \
    funcall_data.argsout = argsout; \
    funcall_data.error  = error; \
    funcall_data.func   = _NAME_RUBYCALL_FUNC_THREADED(rb_module,rb_function); \
    debug_thread("Sent!\n"); \
    pthread_cond_signal(&fcall_requested); \
    debug_thread("Waiting return!\n"); \
    pthread_cond_wait(&fcall_returned, &ruby_context_lock); \
    debug_thread("Returned!\n"); \
    /* this long should be machine register */\
    result = (rb_result_type)(long)funcall_data.result; \
    pthread_mutex_unlock ( &ruby_context_lock); \
    for(i=0;i<nargin;i++){ \
      free(argsin[i].name);\
      free(argsin[i].type);\
    }\
    for(i=0;i<nargout;i++){\
      free(argsout[i].name);\
      free(argsout[i].type);\
    }\
    free(signature);\
    free(str_argin);\
    free(str_argout);\
    return result;\
}; \
\
static void* _NAME_RUBYCALL_FUNC_THREADED(rb_module,rb_function) () {\
    /*osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );*/\
    rb_result_type result = (rb_result_type)0; \
    int ruby_error, i, nargin, nargout; \
    struct arg_desc *argsin, *argsout; \
    OSyncError		**error; \
    nargin=funcall_data.nargin; nargout=funcall_data.nargout; \
    argsin=funcall_data.argsin; argsout=funcall_data.argsout; \
    error=funcall_data.error;

#define END_RUBYCALL_FUNC \
    osync_trace ( TRACE_EXIT, "%s: %p", __func__, (void*)(long)result);\
    return (void*)(long)result;\
error:\
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print (error) );\
    return NULL;\
};

/* CALLBACKS
 *
 * BEGIN_CALLBACK_FUNC is the same of BEGIN_RUBYCALL_FUNC but it already implement the necessary
 * code to be used as callback. The returned from ruby is accessible in (VALUE ruby_result).
 *
 *
 * Also, it defines the function to be exposed inside ruby in order
 * to set the callback block (using DEFINE_SET_CALLBACK_FUNC). This function name is expanded from:
 *
 * NAME_CALLBACK_SET_FUNC(rb_module, rb_function)
 *
 * Which gets:
 *
 * rb_osync_rb_module_set_rb_function_func
 *
 * Besides all the parameters already used in BEGIN_RUBYCALL_FUNC, BEGIN_CALLBACK_FUNC also needs;
 *
 * #define cb_ownertype   The type of the callback owner (i.e.: OSyncPlugin)
 *
 * The method must be registed in ruby:
 *
 * rb_define_module_function ( mOpensync, "osync_objformat_set_destroy_func", NAME_CALLBACK_SET_FUNC(objformat, destroy), -1 );
 *
 */
#define NAME_CALLBACK_SET_FUNC(rb_module, rb_function) rb_osync_##rb_module##_set_##rb_function##_func
#define _NAME_CALLBACK_SET_FUNC(rb_module, rb_function) NAME_CALLBACK_SET_FUNC(rb_module, rb_function)
#define DEFINE_SET_CALLBACK_FUNC(cb_ownertype, rb_module, rb_function) \
VALUE NAME_CALLBACK_SET_FUNC(rb_module, rb_function) ( int argc, VALUE *argv, VALUE self ) {\
    cb_ownertype *arg1 = ( cb_ownertype * ) 0 ;\
    void *argp1 = 0 ;\
    int res1 = 0 ;\
    if ( ( argc < 2 ) || ( argc > 2 ) ) {\
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );\
        SWIG_fail;\
    }\
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_##cb_ownertype, 0 |  0 );\
    if ( !SWIG_IsOK ( res1 ) ) {\
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", #cb_ownertype "*", "osync_" #rb_module "_set_" #rb_function "_func", 1, argv[0] ) );\
    }\
    arg1 = ( cb_ownertype * ) ( argp1 );\
    osync_rubymodule_set_data ( argp1, #rb_function "_func", argv[1] );\
    osync_##rb_module##_set_##rb_function##_func ( arg1, osync_rubymodule_##rb_module##_##rb_function );\
    return Qnil;\
fail:\
    return Qnil;\
}
/* This is the same of RUBYCALL. However, it also get the ruby callback object (rb_function"_func"),
 * call rb_funcall2_protected, method "call", and treat errors */
#define BEGIN_CALLBACK_FUNC \
BEGIN_RUBYCALL_FUNC \
    CAST_ALL_AS_RUBY(ruby_args, argsin, nargin); \
    VALUE callback = osync_rubymodule_get_data (argsin[0].ptr, STR(rb_function) "_func" );\
    VALUE ruby_result = rb_funcall2_protected ( callback, "call", nargin, ruby_args, &ruby_error );\
    if ( ruby_error!=0 ) {\
        osync_error_set (error, OSYNC_ERROR_GENERIC, "Failed to call " STR(rb_module) " " STR(rb_function) "function!\n%s",\
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );\
        goto error;\
    }


#define END_CALLBACK_FUNC 	END_RUBYCALL_FUNC \
 \
_DEFINE_SET_CALLBACK_FUNC(cb_ownertype, rb_module, rb_function)

#define _DEFINE_SET_CALLBACK_FUNC(cb_ownertype, rb_module, rb_function) DEFINE_SET_CALLBACK_FUNC(cb_ownertype, rb_module, rb_function)


// VALUE rb_protect(VALUE (*func)(VALUE), VALUE args, int *error) {
//     VALUE result;
//     pthread_mutex_lock ( &ruby_context_lock );
//     result = rb_protect(func, args, error);
//     pthread_mutex_unlock ( &ruby_context_lock );
//     return result;
// }
/*
VALUE rb_protect(VALUE (*func)(VALUE), VALUE args, int *error) {
    VALUE result;
    debug_thread("Locking!\n");
    pthread_mutex_lock ( &ruby_context_lock);
    debug_thread("Waiting for my time!\n");
    // Wait for ruby to run
    if (!ruby_running) {
	debug_thread("Ruby thread is not running. Waiting\n");
	pthread_cond_wait(&fcall_ruby_running, &ruby_context_lock);
	debug_thread("Ruby thread is running!\n");
    }
    funcall_args.func = func;
    funcall_args.arg = args;
    funcall_args.error = error;
    debug_thread("Sent!\n");
    pthread_cond_signal(&fcall_requested);
    debug_thread("Waiting return!\n");
    pthread_cond_wait(&fcall_returned, &ruby_context_lock);
    debug_thread("Returned!\n");
    result = funcall_args.result;
    pthread_mutex_unlock ( &ruby_context_lock);
    return result;
}*/

VALUE rb_funcall2_wrapper ( VALUE* params ) {
    VALUE result;
    pthread_t this_thread = pthread_self();
    char  *name;

    debug_fcall("STACK: %p ", &result);
    name = RSTRING_PTR(rb_funcall2 (params[0], rb_intern("inspect"), 0,0));
    debug_fcall("%s.%s()...",name, ( char* )params[1]);

    result = rb_funcall2 ( params[0], rb_intern ( ( char* ) params[1] ), ( int ) params[2], ( VALUE* ) params[3] );
    debug_fcall("returned!");

    debug_fcall("GarbageCollecting...");
    rb_funcall (rb_mGC, rb_intern ("start"), 0,  NULL);
    debug_fcall("done!");

    debug_fcall("\n");
    return result;
}

VALUE rb_funcall2_protected ( VALUE recv, const char* method, int argc, VALUE* args, int* status ) {
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
        message = Qnil;
    }
    return RSTRING_PTR ( message );
}

void unregister_and_free(gpointer data) {
    rb_gc_unregister_address(data);
    free(data);
};

void osync_rubymodule_set_data ( void* ptr, char const *key, VALUE data ) {
    GHashTable *ptr_data;
    VALUE saved_data;

//     fprintf(stderr, "%p[\"%s\"@%p]  = %lu\n", ptr, key, &key, data);

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

VALUE osync_rubymodule_get_data ( void* ptr, char const *key ) {
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

void osync_rubymodule_clean_data ( void* ptr ) {
    g_hash_table_remove ( rubymodule_data, ptr );
}

VALUE rb_osync_rubymodule_get_data ( int argc, VALUE *argv, VALUE self ) {
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

VALUE rb_osync_rubymodule_set_data ( int argc, VALUE *argv, VALUE self ) {
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

VALUE rb_osync_rubymodule_clean_data ( int argc, VALUE *argv, VALUE self ) {
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

static void osync_rubymodule_objtype_sink_connect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "connect_func" );
    assert(callback);

    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = IND_VALUE(data);
    /*VALUE result = */

    rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink connect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_get_changes ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "get_changes_func" );
    assert(callback);

    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = BOOLR ( slow_sync );
    args[4] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink get_changes_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_commit ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "commit_func" );
    assert(callback);


    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink commit_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_committed_all ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "commited_all" );
    assert(callback);


    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink commited_all function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_read ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p, %p)", __func__, sink , info, ctx, change, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "read_func" );
    assert(callback);

    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
    args[4] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink read_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:
    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_sync_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "sync_done_func" );
    assert(callback);

    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink sync_done function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_connect_done ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %i, %p)", __func__, sink, info, ctx, slow_sync, data );
    int status;
    OSyncError *error = 0;

    VALUE callback = osync_rubymodule_get_data ( sink, "connect_done" );
    assert(callback);


    VALUE args[5];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = BOOLR ( slow_sync );
    args[4] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 5, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink disconnect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}

static void osync_rubymodule_objtype_sink_disconnect ( OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p, %p)", __func__, sink, info, ctx, data );
    int status;
    OSyncError *error = 0;
    VALUE callback = osync_rubymodule_get_data ( sink, "disconnect" );
    assert(callback);


    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
    args[3] = IND_VALUE(data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink disconnect_fn function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s", __func__ );
    return;
error:

    osync_context_report_osyncerror ( ctx, error );
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( &error ) );
    osync_error_unref ( &error );
    return;
}


/* In initialize, we get the config for the plugin. Here we also must register
 * all _possible_ objtype sinks. */
// static void *osync_rubymodule_plugin_initialize ( OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error ) {
//     int status;
//     osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );
//
//     VALUE callback = osync_rubymodule_get_data ( plugin, "initialize_func" );
//     assert(callback);
//
//
//     VALUE args[2];
//     args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
//     args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
//     VALUE result = rb_funcall2_protected ( callback, "call", 2, args, &status );
//     if ( status!=0 ) {
//         osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin initializing function!\n%s",
//                           osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
//         goto error;
//     }
//     VALUE *pplugin_data = malloc(sizeof(VALUE));
//     *pplugin_data = result;
//     rb_gc_register_address(pplugin_data);
//
//     osync_trace ( TRACE_EXIT, "%s: %lu", __func__, *pplugin_data );
//
//     return pplugin_data;
// error:
//
//     osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
//     return;
// }

#define rb_result_type 	void*
#define rb_module	plugin
#define rb_function	initialize
#define rb_sig      	OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error
#define rb_argins  	plugin, info
#define rb_argouts  	error
#define cb_ownertype  	OSyncPlugin
BEGIN_CALLBACK_FUNC
    VALUE *pplugin_data = malloc(sizeof(VALUE));
     *pplugin_data = ruby_result;
     rb_gc_register_address(pplugin_data);
    result = pplugin_data;
END_CALLBACK_FUNC
#undef rb_result_type
#undef rb_module
#undef rb_function
#undef rb_sig
#undef rb_argins
#undef rb_argouts
#undef cb_ownertype

static void osync_rubymodule_plugin_finalize ( OSyncPlugin *plugin, void* plugin_data ) {
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, plugin, plugin_data );

    int status;
    VALUE callback = osync_rubymodule_get_data ( plugin, "finalize_func" );
    assert(callback);


    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    args[1] = IND_VALUE(plugin_data);
    /*VALUE result = */
    rb_funcall2_protected ( callback, "call", 2, args, &status );
    /* there is no error return, no one is interested if finalize fails
    if (status!=0) {
      	osync_error_set(error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin finalize function!\n%s",
    	  osync_rubymodule_error_bt(__FILE__, __func__,__LINE__));
    goto error;
    } */
    if (plugin_data) {
        rb_gc_unregister_address(plugin_data);
	free(plugin_data);
    }


    osync_trace ( TRACE_EXIT, "%s", __func__ );
}

/* Here we actually tell opensync which sinks are available. For this plugin, we
 * just report all objtype as available. Since the resource are configured like this. */
static osync_bool osync_rubymodule_plugin_discover ( OSyncPlugin *plugin, OSyncPluginInfo *info, void* plugin_data, OSyncError **error ) {
    osync_trace ( TRACE_ENTRY, "%s(%p, %p, %p)", __func__, plugin, info, error );

    VALUE callback = osync_rubymodule_get_data ( plugin, "discover_func" );
    int status;


    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( plugin ), SWIGTYPE_p_OSyncPlugin, 0 |  0 );
    args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
    args[2] = IND_VALUE(plugin_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call plugin discover function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }


    osync_trace ( TRACE_EXIT, "%s: %i", __func__, RBOOL(result) );
    return RBOOL(result);
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static void *osync_rubymodule_objformat_initialize ( OSyncObjFormat *format, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "initialize_func" );


    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_funcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initialize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    VALUE *puser_data = malloc(sizeof(VALUE));
    *puser_data = result;
    rb_gc_register_address(puser_data);

    osync_trace ( TRACE_EXIT, "%s: %lu", __func__, *puser_data );

    return puser_data;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

static osync_bool osync_rubymodule_objformat_finalize ( OSyncObjFormat *format, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "finalize_func" );



    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    VALUE result = rb_funcall2_protected ( callback, "call", 1, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat finalize function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    // Should I need this (and ruby unref)?
    //format_data->data=NULL;
    if (user_data) {
        rb_gc_unregister_address(user_data);
	free(user_data);
    }


    osync_trace ( TRACE_EXIT, "%s: %i", __func__, TRUE );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static OSyncConvCmpResult osync_rubymodule_objformat_compare ( OSyncObjFormat *format, const char *leftdata, unsigned int leftsize, const char *rightdata, unsigned int rightsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "compare_func" );



    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( leftdata, leftsize );
    args[2] = SWIG_FromCharPtrAndSize ( rightdata, rightsize );
    args[3] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_FIXNUM ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of compare should be a FixNum!\n" );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s: %i", __func__, FIX2INT ( result ) );
    return FIX2INT ( result );
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return 0;
}

static char *osync_rubymodule_objformat_print ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "print_func" );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of print should be a String!\n" );
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s: %p", __func__, RSTRING_PTR ( result ) );
    return RSTRING_PTR ( result );
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return NULL;
}

static time_t osync_rubymodule_objformat_revision ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "revision_func" );



    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ( !IS_TIME ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of revision should be a Time!\n" );
        goto error;
    }

    result = rb_funcall2_protected ( result, "to_i", 0, NULL, &status );
    if ( ( status!=0 ) || !IS_FIXNUM ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to convert time to a number!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }


    osync_trace ( TRACE_EXIT, "%s: %li", __func__,FIX2LONG ( result ) );
    return FIX2LONG ( result );
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return 0;
}

static osync_bool osync_rubymodule_objformat_destroy ( OSyncObjFormat *format, char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "destroy_func" );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;


    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}
/*
static osync_bool osync_rubymodule_objformat_copy ( OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
    VALUE callback = osync_rubymodule_get_data ( format, "copy_func" );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( input, insize );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result of copy should be a String!\n" );
        goto error;
    }

    *output = RSTRING_PTR ( result );
    *outpsize = RSTRING_LEN ( result );

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}*/

static osync_bool osync_rubymodule_objformat_duplicate ( OSyncObjFormat *format, const char *uid, const char *input, unsigned int insize, char **newuid, char **output, unsigned int *outsize, osync_bool *dirty, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "duplicate_func" );


    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( uid, strlen ( uid ) );
    args[2] = SWIG_FromCharPtrAndSize ( input, insize );
    args[3] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_ARRAY ( result ) || ( RARRAY_LEN ( result ) != 3 ) ||
            !IS_STRING ( rb_ary_entry ( result, 0 ) ) ||
            !IS_STRING ( rb_ary_entry ( result, 1 ) ) ||
            !IS_BOOL ( rb_ary_entry ( result, 2 ) )
       ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should of print should be an Array with [newuid:string, output:string, dirty:bool] !\n" );
        goto error;
    }

    *newuid = RSTRING_PTR ( rb_ary_entry ( result, 0 ) );
    *output = RSTRING_PTR ( rb_ary_entry ( result, 1 ) );
    *dirty  = RBOOL ( rb_ary_entry ( result, 2 ) );


    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_create ( OSyncObjFormat *format, char **data, unsigned int *size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "create_func" );

    VALUE args[2];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 2, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result of create should be a String!\n" );
        goto error;
    }
    *data = RSTRING_PTR ( result );
    *size = RSTRING_LEN ( result );

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_marshal ( OSyncObjFormat *format, const char *input, unsigned int inpsize, OSyncMarshal *marshal, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "marshal_func" );

    VALUE args[4];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( input, inpsize );
    args[2] = SWIG_NewPointerObj(SWIG_as_voidptr(marshal), SWIGTYPE_p_OSyncMarshal, 0 |  0 );
    args[3] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 4, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_demarshal ( OSyncObjFormat *format, OSyncMarshal *marshal, char **output, unsigned int *outpsize, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "demarshal_func" );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_NewPointerObj(SWIG_as_voidptr(marshal), SWIGTYPE_p_OSyncMarshal, 0 |  0 );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat initializing function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !IS_STRING ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result of demarshal should be a String!\n" );
        goto error;
    }

    *output = RSTRING_PTR ( result );
    *outpsize = RSTRING_LEN ( result );

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
}

static osync_bool osync_rubymodule_objformat_validate ( OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error ) {
    int status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );

    VALUE callback = osync_rubymodule_get_data ( format, "validate_func" );

    VALUE args[3];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( format ), SWIGTYPE_p_OSyncObjFormat, 0 |  0 );
    args[1] = SWIG_FromCharPtrAndSize ( data, size );
    args[2] = IND_VALUE(user_data);
    VALUE result = rb_funcall2_protected ( callback, "call", 3, args, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_INITIALIZATION, "Failed to call objformat validate function!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( result ) )
        goto error;

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:

    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( error ) );
    return FALSE;
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





#define rb_result_type 	osync_bool
#define rb_module	objformat
#define rb_function	copy
#define rb_sig      	OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error
#define rb_argins  	format, input, insize, error, user_data
#define rb_argouts  	output, outpsize
#define cb_ownertype  	OSyncObjFormat
BEGIN_CALLBACK_FUNC
    if ( !IS_STRING ( ruby_result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result of copy should be a String!\n" );
        goto error;
    }
    *((char**)argsout[0].ptr) = RSTRING_PTR ( ruby_result );
    *((unsigned int*)argsout[1].ptr) = RSTRING_LEN ( ruby_result );
    result = TRUE;
END_CALLBACK_FUNC
#undef rb_result_type
#undef rb_module
#undef rb_function
#undef rb_sig
#undef rb_argins
#undef rb_argouts
#undef cb_ownertype

// static void* osync_rubymodule_objformat_copy_threaded(struct threaded_funcall data);
// static void* osync_rubymodule_objformat_copy_sync ( OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error) {
//     //int nargin = strspn("OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error", ",");
//     const char *signature  = strdup("OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error");
//     const char *str_argin  = strdeblank(strdup("format, input, insize, error, user_data "));
//     const char *str_argout  = strdeblank(strdup("format, input, insize, error, user_data "));
//     int nargin = strlen(str_argin)==0? 0 : strcount(str_argin, ',')+1;
//     int nargin = strlen(str_argout)==0? 0 : strcount(str_argout, ',')+1;
//     int i;
//     void* result;
//     struct arg_desc argsin[nargin];
//     struct arg_desc argsout[nargout];
//     save_args(signature, str_argin,  nargin,  argsin,  format, input, insize, user_data);
//     save_args(signature, str_argout, nargout, argsout, output, outpsize);
//
//     debug_thread("Locking!\n");
//     pthread_mutex_lock ( &ruby_context_lock);
//     debug_thread("Waiting for my time!\n");
//     // Wait for ruby to run
//     if (!ruby_running) {
// 	debug_thread("Ruby thread is not running. Waiting\n");
// 	pthread_cond_wait(&fcall_ruby_running, &ruby_context_lock);
// 	debug_thread("Ruby thread is running!\n");
//     }
//     funcall_data.nargin  = nargin;
//     funcall_data.nargout = nargout;
//     funcall_data.argsin  = argsin;
//     funcall_data.argsout = argsout;
//     funcall_data.error  = error;
//     funcall_data.func   = osync_rubymodule_objformat_copy_threaded;
//     debug_thread("Sent!\n");
//     pthread_cond_signal(&fcall_requested);
//     debug_thread("Waiting return!\n");
//     pthread_cond_wait(&fcall_returned, &ruby_context_lock);
//     debug_thread("Returned!\n");
//     result = (void*)funcall_data.result;
//     pthread_mutex_unlock ( &ruby_context_lock);
//
//     // Free argsin and argsout
//     for(i=0;i<nargin;i++){
//       g_free(argsin[i].name);
//       g_free(argsin[i].type);
//     }
//     for(i=0;i<nargout;i++){
//       g_free(argsout[i].name);
//       g_free(argsout[i].type);
//     }
//     free(signature);
//     free(str_argin);
//     return result;
// }
// void* osync_rubymodule_objformat_copy_threaded(struct threaded_funcall data) {
//     //TODO: fill
//     //osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, error );
//     void  *result;
//     int status;
//     int i;
//     VALUE ruby_result;
//     VALUE callback = osync_rubymodule_get_data (data.argsin[0].ptr, "copy_func" );
//     VALUE ruby_args[data.nargin];
//
//     for (i=0; i<data.nargin; i++) {
//       swig_type_info *type = SWIG_TypeQuery(data.argsin[i].type);
//       if (type) {
// 	ruby_args[i]=SWIG_NewPointerObj ( data.argsin[i].ptr, type, 0 |  0 );
//       } else {
// 	ruby_args[i]=Qnil;
//       }
//     }
//     ruby_result = rb_funcall2_protected ( callback, "call", data.nargin, ruby_args, &status );
//     if ( status!=0 ) {
//         osync_error_set (data.error, OSYNC_ERROR_INITIALIZATION, "Failed to call " "objformat" " " "copy" "function!\n%s",
//                           osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
//         goto error;
//     }
//
//     /* written */
//     if ( !IS_STRING ( ruby_result ) ) {
//         osync_error_set ( data.error, OSYNC_ERROR_GENERIC, "The result of copy should be a String!\n" );
//         goto error;
//     }
//     *((char**)data.argsout[0].ptr) = RSTRING_PTR ( ruby_result );
//     *((unsigned int*)data.argsout[1].ptr) = RSTRING_LEN ( ruby_result );
//     result = (void*)TRUE;
//
//     osync_trace ( TRACE_EXIT, "%s: %p", __func__, result);
//     return result;
// error:
//     osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print ( data.error ) );
//     return NULL;
// }

// CALLBACK_FUNC(void*, objformat, copy, \
// 	(OSyncObjFormat *format, const char *input, unsigned int insize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error),\
// 	(format, input, insize, user_data),(output, outpsize))


#define rb_result_type 	osync_bool
#define rb_module	sync
#define rb_function	info
#define rb_sig      	OSyncPluginEnv *env, OSyncError **error
#define rb_argins  	env
#define rb_argouts  	0
BEGIN_RUBYCALL_FUNC
    CAST_ALL_AS_RUBY(ruby_args, argsin, nargin);

    fprintf(stderr, "%lu\n", pthread_self());
    fprintf(stderr,"teste!!! %i, %lu %s\n", nargin, ruby_args[0], RSTRING_PTR(rb_funcall(ruby_args[0], rb_intern("to_s"), 0)));


    int status;
    rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_require, ( VALUE ) RUBY_BASE_FILE, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to load module ruby file '%s'!\n%s",RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE plugin_class = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_path2class, ( VALUE ) RUBY_PLUGIN_CLASS, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Error on finding %s class in %s!\n%s",RUBY_PLUGIN_CLASS, RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( plugin_class == Qnil ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Class %s not found!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE ruby_result = rb_funcall2_protected ( plugin_class,"get_sync_info", nargin, ruby_args, &status);
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_sync_info!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( ruby_result ) ) {
        goto error;
    }
    result = TRUE;

END_RUBYCALL_FUNC
#undef rb_result_type
#undef rb_module
#undef rb_function
#undef rb_sig
#undef rb_argins
#undef rb_argouts


osync_bool get_sync_info ( OSyncPluginEnv *env, OSyncError **error ) {
//    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, env );
    return NAME_RUBYCALL_FUNC(sync,info)(env, error);
}

osync_bool get_format_infox ( OSyncFormatEnv *env, OSyncError **error ) {
    int   status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, env );

    rubymodule_ruby_needed();

    rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_require, ( VALUE ) RUBY_BASE_FILE, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to load module ruby file '%s'!\n%s",RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE plugin_class = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_path2class, ( VALUE ) RUBY_FORMAT_CLASS, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Error on finding %s class in %s!\n%s",RUBY_FORMAT_CLASS, RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( plugin_class == Qnil ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Class %s not found!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( env ), SWIGTYPE_p_OSyncFormatEnv, 0 |  0 );
    VALUE result = rb_funcall2_protected ( plugin_class,"get_format_info",1, args,&status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_format_info!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ( !RBOOL ( result ) ) {
        goto error;
    }

    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );

    return FALSE;
}

osync_bool get_conversion_infox ( OSyncFormatEnv *env, OSyncError **error ) {
    int   status;
    osync_trace ( TRACE_ENTRY, "%s(%p)", __func__, env );

    rubymodule_ruby_needed();

    rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_require, ( VALUE ) RUBY_BASE_FILE, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to load module ruby file '%s'!\n%s",RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    VALUE plugin_class = rb_protect ( ( VALUE ( * ) ( VALUE ) ) rb_path2class, ( VALUE ) RUBY_FORMAT_CLASS, &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Error on finding %s class in %s!\n%s",RUBY_FORMAT_CLASS, RUBY_BASE_FILE,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( plugin_class == Qnil ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Class %s not found!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    VALUE args[1];
    args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( env ), SWIGTYPE_p_OSyncFormatEnv, 0 |  0 );
    VALUE result = rb_funcall2_protected ( plugin_class,"get_conversion_info",1, args,&status );
    if ( status!=0 ) {
        fprintf ( stderr, "%s\n", osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_conversion_info!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }

    if ( !RBOOL ( result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "%s.get_conversion_info returned false or nil (which means problems)!\n%s", RUBY_FORMAT_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }


    osync_trace ( TRACE_EXIT, "%s: true", __func__ );
    return TRUE;
error:
    osync_trace ( TRACE_ERROR, "Unable to register: %s", osync_error_print ( error ) );

    return FALSE;
}

/*
typedef void* (* threaded_func) (va_list args);
// struct threaded_funcall {
//   static threaded_func func;
//   void	 **args;
//   void   *result;
// }
// static struct threaded_funcall funcall_data;
void* threaded(threaded_func func, ...) {
    // LocK
    // wait free?
    nargs;
    funcall_data.func = func;
    funcall_data.va_list = va_list;
    //signal
    return funcall.result;
    // unlock
}*/
// #define DEFINE_CALLBACK_FUNC(result_type, module, function, args...) \
// result_type osync_rubymodule_##module##_##function##_sync (type *module, args) {\
//        char *str = strdup()
//        int nargin =
//        save_arguments()
//     pthread_mutex_lock ( &ruby_context_lock);
//     funcall_args.func = func;
//     funcall_args.arg = args;
//     funcall_args.error = error;
//     debug_thread("Sent!\n");
//     pthread_cond_signal(&fcall_requested);
//     debug_thread("Waiting return!\n");
//     pthread_cond_wait(&fcall_returned, &ruby_context_lock);
//     debug_thread("Returned!\n");
//     result = funcall_args.result;
//     pthread_mutex_unlock ( &ruby_context_lock);
// }\
// result osync_rubymodule_##module##_##function (type *module, args) {\
//     osync_trace ( TRACE_ENTRY, "%s(...)", __func__);\
//     int status;\
//     OSyncError *error = 0;\
//     VALUE callback = osync_rubymodule_get_data (module, "read_func" );\
//     assert(callback);\
//     VALUE args[5];
//     args[0] = SWIG_NewPointerObj ( SWIG_as_voidptr ( sink ), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 );
//     args[1] = SWIG_NewPointerObj ( SWIG_as_voidptr ( info ), SWIGTYPE_p_OSyncPluginInfo, 0 |  0 );
//     args[2] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncContext, 0 |  0 );
//     args[3] = SWIG_NewPointerObj ( SWIG_as_voidptr ( ctx ), SWIGTYPE_p_OSyncChange, 0 |  0 );
//     args[4] = IND_VALUE(data);
//     rb_funcall2_protected ( callback, "call", 5, args, &status );
//     if ( status!=0 ) {
//         osync_error_set ( &error, OSYNC_ERROR_GENERIC, "Failed to call sink read_fn function!\n%s",
//                           osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
//         goto error;
//     }
//
//     osync_trace ( TRACE_EXIT, "%s", __func__ );
//     return;
// \
// }

/** Plugin */


//DEFINE_SET_CALLBACK_FUNC(OSyncPlugin, plugin, initialize)
DEFINE_SET_CALLBACK_FUNC(OSyncPlugin, plugin, finalize)
DEFINE_SET_CALLBACK_FUNC(OSyncPlugin, plugin, discover)

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

DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, read)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, sync_done)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, connect_done)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, disconnect)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, connect)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, get_changes)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, commit)
DEFINE_SET_CALLBACK_FUNC(OSyncObjTypeSink, objtype_sink, committed_all)

/** ObjectFormat */

DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, initialize)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, finalize)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, compare)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, destroy)
//DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, copy)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, duplicate)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, create)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, print)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, revision)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, marshal)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, demarshal)
DEFINE_SET_CALLBACK_FUNC(OSyncObjFormat, objformat, validate)

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

void rubymodule_initialize() {

/*    int page = sysconf(_SC_PAGE_SIZE);
    mprotect((void *)((unsigned long)&STACK_END_ADDRESS & ~(page - 1)), page, PROT_READ | PROT_WRITE | PROT_EXEC);*/
    /* Initialize Ruby env */
//     RUBY_INIT_STACK;
//     ruby_init();
//     ruby_init_loadpath();
//     ruby_script ( RUBY_SCRIPTNAME );
    // SWIG initialize (include the module)
    Init_opensync();

    rb_define_module_function ( mOpensync, "osync_rubymodule_get_data", rb_osync_rubymodule_get_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_set_data", rb_osync_rubymodule_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_rubymodule_clean_data", rb_osync_rubymodule_clean_data, -1 );

    rb_define_module_function ( mOpensync, "osync_plugin_set_initialize_func", rb_osync_plugin_set_initialize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_finalize_func", rb_osync_plugin_set_finalize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_plugin_set_discover_func", rb_osync_plugin_set_discover_func, -1 );

    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_userdata", rb_osync_objtype_sink_set_userdata, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_func", rb_osync_objtype_sink_set_connect_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_get_changes_func", rb_osync_objtype_sink_set_get_changes_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_commit_func", rb_osync_objtype_sink_set_commit_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_committed_all_func", rb_osync_objtype_sink_set_committed_all_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_read_func", rb_osync_objtype_sink_set_read_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_sync_done_func", rb_osync_objtype_sink_set_sync_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_connect_done_func", rb_osync_objtype_sink_set_connect_done_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objtype_sink_set_disconnect_func", rb_osync_objtype_sink_set_disconnect_func, -1 );

//     rb_define_module_function ( mOpensync, "osync_objformat_get_data",  rb_osync_objformat_get_data, -1 );
//     rb_define_module_function ( mOpensync, "osync_objformat_set_data" , rb_osync_objformat_set_data, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_initialize_func", rb_osync_objformat_set_initialize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_finalize_func", rb_osync_objformat_set_finalize_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_compare_func", rb_osync_objformat_set_compare_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_destroy_func", rb_osync_objformat_set_destroy_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_copy_func", rb_osync_objformat_set_copy_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_duplicate_func", rb_osync_objformat_set_duplicate_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_create_func", rb_osync_objformat_set_create_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_print_func", rb_osync_objformat_set_print_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_revision_func", rb_osync_objformat_set_revision_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_marshal_func", rb_osync_objformat_set_marshal_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_demarshal_func", rb_osync_objformat_set_demarshal_func, -1 );
    rb_define_module_function ( mOpensync, "osync_objformat_set_validate_func", rb_osync_objformat_set_validate_func, -1 );

    rb_define_module_function ( mOpensync, "osync_converter_new", rb_osync_converter_new, -1 );

    rb_define_const(mOpensync, "OPENSYNC_RUBYPLUGIN_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYPLUGIN_DIR));
    rb_define_const(mOpensync, "OPENSYNC_RUBYFORMATS_DIR", SWIG_FromCharPtr (OPENSYNC_RUBYFORMATS_DIR));

    rubymodule_data = g_hash_table_new_full ( g_direct_hash, g_direct_equal, NULL, ( GDestroyNotify ) g_hash_table_destroy );
}

void rubymodule_finalize() {
    g_hash_table_destroy ( rubymodule_data );
    RUBY_PROLOGUE
    ruby_finalize();
    RUBY_EPILOGUE
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
    pthread_cond_broadcast(&fcall_ruby_running);
    while (ruby_running) {
       debug_thread("Waiting a command!\n");
       pthread_cond_wait(&fcall_requested, &ruby_context_lock);
       RUBY_PROLOGUE
       // TODO: Now call the C Function
       void* result = funcall_data.func();
       funcall_data.result = result;
       //funcall_data.result = rb_protect(funcall_args.func, funcall_args.arg, funcall_args.error);
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

/*
void rubymodule_ruby_needed () {
    pthread_mutex_lock ( &ruby_context_lock );
    if (!(ruby_uses++>0)) {
	rubymodule_initialize();
    }
    pthread_mutex_unlock ( &ruby_context_lock );
}

void rubymodule_ruby_unneeded () {
    pthread_mutex_lock ( &ruby_context_lock );
    if (!(--ruby_uses>0)) {
	rubymodule_finalize();
    }
    pthread_mutex_unlock ( &ruby_context_lock );
}*/

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



