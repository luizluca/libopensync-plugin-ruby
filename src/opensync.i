%module(docstring="Bindings for the OpenSync library") opensync
%feature("autodoc", "1");
//%include "cstring.i"
//%include "cpointer.i"

%{
#include <opensync/opensync.h>
#include <opensync/opensync-capabilities.h>
#include <opensync/opensync-data.h>
#include <opensync/opensync-engine.h>
#include <opensync/opensync-format.h>
#include <opensync/opensync-group.h>
#include <opensync/opensync-helper.h>
#include <opensync/opensync-plugin.h>
#include <opensync/opensync-version.h>
#include <opensync/opensync-xmlformat.h>

%}

/* Convert Booleans  */
#ifdef SWIGRUBY
#define RUBY_EMBEDDED
%initstack;

%typemap(in) osync_bool {
 $1 = ($input==Qfalse ? FALSE : TRUE);
}
%typemap(out) osync_bool {
 $result = ($1==FALSE ? Qfalse : Qtrue);
}

/* memory leak */
%typemap(in) void* {
  $1 = malloc(sizeof(VALUE));
  rb_gc_register_address($1);
  *(VALUE*)$1=$input;
}
%typemap(out) void* {
  $result = $1 ? *(VALUE*)$1 : Qnil;
}

%typemap(in) time_t {
  $1 = FIX2LONG(rb_funcall($input, rb_intern("to_i"), 0));
}
%typemap(out) time_t {
  $result = rb_funcall(rb_cTime, rb_intern("at"), 1, LONG2FIX($1));
}

/* Translate into ruby string */
%typemap(in) (char *data, unsigned int size) {
 $1 = RSTRING_PTR($input);
 $2 = (unsigned int) RSTRING_LEN($input);
};

/* Translate into ruby string */
%typemap(in) (const void *value, unsigned int size) {
 $1 = RSTRING_PTR($input);
 $2 = (unsigned int) RSTRING_LEN($input);
};

/* Translate into ruby string */
%typemap(in) (char *buffer, unsigned int size) {
 $1 = RSTRING_PTR($input);
 $2 = (unsigned int) RSTRING_LEN($input);
};

/* Convert out arguments into a single ruby value */
%typemap(in, numinputs=0) (char **buffer, unsigned int *size) (char* temp_buffer, unsigned int temp_size) {
   $1 = &temp_buffer;
   $2 = &temp_size;
}
%typemap(argout) (char **buffer, unsigned int *size) {
  $result = SWIG_FromCharPtrAndSize(temp_buffer$argnum, temp_size$argnum);
};

/* Convert out arguments into a single ruby value */
%typemap(in, numinputs=0) (void **value, unsigned int *size) (void* temp_value, unsigned int temp_size) {
   $1 = &temp_value;
   $2 = &temp_size;
}
%typemap(argout) (void **value, unsigned int *size) {
  $result = SWIG_FromCharPtrAndSize(temp_value$argnum, temp_size$argnum);
};

/* Convert out arguments into a single ruby value */
%typemap(in, numinputs=0) (char **value) (char* temp_value) {
   $1 = &temp_value;
}
%typemap(argout) (char **value) {
  $result = SWIG_FromCharPtr(temp_value$argnum);
};

/* Convert out arguments into a single ruby value */
%typemap(in, numinputs=0) (osync_bool *issame) (osync_bool temp_issame) {
   $1 = &temp_issame;
}
%typemap(argout) (osync_bool *issame) {
  $result = (temp_issame$argnum==FALSE ? Qfalse : Qtrue);
};

/* error parameter marked as optional. No need to be supplied*/
%typemap(default) OSyncError** error (OSyncError* error) {
  error = NULL;
  $1 = &error;
}

%typemap(freearg) OSyncError** error {
  if (error$argnum) {
    rb_raise(rb_eStandardError, "%s",osync_error_print(&error$argnum));
    osync_error_unref(&error$argnum);
    SWIG_fail;
  }
}

/* Convert each syncList to its own type for each method*/
/* TODO: complete this list */
%typemap(out) OSyncList* %{
  VALUE array = rb_ary_new();
  OSyncList *list, *item;
  list = $1;
  for (item = list; item; item = item->next) {
#define $symname
#if defined(osync_plugin_env_get_plugins)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncPlugin, 0 |  0 ));
#elif defined(osync_plugin_info_get_objtype_sinks)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncObjTypeSink, 0 |  0 ));
#elif defined(osync_plugin_config_get_advancedoptions)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncPluginAdvancedOption, 0 |  0 ));
#elif defined(osync_plugin_config_get_resources)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncPluginResource, 0 |  0 ));
#elif defined(osync_plugin_advancedoption_get_parameters)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncPluginAdvancedOptionParameter, 0 |  0 ));
#elif defined(osync_plugin_advancedoption_get_valenums)
    rb_ary_push(array, SWIG_FromCharPtr(SWIG_as_voidptr(item->data)));
#elif defined(osync_plugin_advancedoption_param_get_valenums)
    rb_ary_push(array, SWIG_FromCharPtr(SWIG_as_voidptr(item->data)));
#elif defined(osync_plugin_resource_get_objformat_sinks)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncObjFormatSink, 0 |  0 ));
#elif defined(osync_objtype_sink_get_objformat_sinks)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncObjFormatSink, 0 |  0 ));
#elif defined(osync_converter_path_get_edges)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 ));
#elif defined(osync_format_env_get_objformats)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncObjFormat, 0 |  0 ));
#elif defined(osync_format_env_find_converters)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 ));
#elif defined(osync_format_env_find_caps_converters)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncCapsConverter, 0 |  0 ));
#elif defined(osync_format_env_get_converters)
    rb_ary_push(array, SWIG_NewPointerObj(SWIG_as_voidptr(item->data), SWIGTYPE_p_OSyncFormatConverter, 0 |  0 ));
#elif defined(osync_hashtable_get_deleted)
    rb_ary_push(array, SWIG_FromCharPtr(SWIG_as_voidptr(item->data)));
#else
    rb_exc_raise(rb_exc_new2(rb_eArgError, "SWIG typemap for OSyncList in '$symname' is not implemented yet. Add it to swig interface"));
#endif
#undef $symname
  }
  osync_list_free(list);
  $result = array;
%}

#endif

/* include manually here because opensync_error.h is not supported by swig */
typedef enum {
	OSYNC_NO_ERROR = 0,
	OSYNC_ERROR_GENERIC = 1,
	OSYNC_ERROR_IO_ERROR = 2,
	OSYNC_ERROR_NOT_SUPPORTED = 3,
	OSYNC_ERROR_TIMEOUT = 4,
	OSYNC_ERROR_DISCONNECTED = 5,
	OSYNC_ERROR_FILE_NOT_FOUND = 6,
	OSYNC_ERROR_EXISTS = 7,
	OSYNC_ERROR_CONVERT = 8,
	OSYNC_ERROR_MISCONFIGURATION = 9,
	OSYNC_ERROR_INITIALIZATION = 10,
	OSYNC_ERROR_PARAMETER = 11,
	OSYNC_ERROR_EXPECTED = 12,
	OSYNC_ERROR_NO_CONNECTION = 13,
	OSYNC_ERROR_TEMPORARY = 14,
	OSYNC_ERROR_LOCKED = 15,
	OSYNC_ERROR_PLUGIN_NOT_FOUND = 16
} OSyncErrorType;

%{
#undef SWIG_Error
#define SWIG_Error(code, msg)            		rb_raise(SWIG_Ruby_ErrorType(code), "%s", msg)
%}

%include "opensync/opensync.h"
%include "opensync/opensync-plugin.h"
%include "opensync/plugin/opensync_context.h"
%include "opensync/plugin/opensync_plugin.h"
%include "opensync/plugin/opensync_plugin_env.h"
%include "opensync/plugin/opensync_plugin_info.h"
%include "opensync/plugin/opensync_plugin_config.h"
%include "opensync/plugin/opensync_plugin_advancedoptions.h"
%include "opensync/plugin/opensync_plugin_authentication.h"
%include "opensync/plugin/opensync_plugin_connection.h"
%include "opensync/plugin/opensync_plugin_localization.h"
%include "opensync/plugin/opensync_plugin_resource.h"
%include "opensync/plugin/opensync_objtype_sink.h"
%include "opensync/opensync-format.h"
%include "opensync/opensync-version.h"
%include "opensync/format/opensync_caps_converter.h"
%include "opensync/format/opensync_converter.h"
%include "opensync/format/opensync_format_env.h"
%include "opensync/format/opensync_objformat.h"
%include "opensync/format/opensync_objformat_sink.h"
%include "opensync/format/opensync_merger.h"
%include "opensync/common/opensync_marshal.h"
%include "opensync/data/opensync_data.h"
%include "opensync/data/opensync_change.h"
%include "opensync/debug/opensync_trace.h"
%include "opensync/helper/opensync_sink_state_db.h"
%include "opensync/helper/opensync_hashtable.h"
%include "opensync/version/opensync_version.h"