#!/usr/bin/ruby
#
# Generate all callback functions
#
# TODO: resolver bugs
#       tratamento do char* especial e do bool
#       arrumar formatacao do printf

class String
    def trim
	return self.sub(/^[[:blank:]]/,"").sub(/[[:blank:]]$/,"")
    end
end
$ruby_methods={}

#
# Called at the end of this script to create the method
# responsible for registering C world ruby methods
#
def define_Init_rubymodule_callbacks
puts <<EOF
void Init_rubymodule_callbacks() {
#{
    code=[]
    $ruby_methods.keys.sort.each {|ruby_land_method|
	code << "    rb_define_module_function ( mOpensync, \"#{ruby_land_method}\", #{$ruby_methods[ruby_land_method]}, -1 );"
    }
    code.join("\n")
}
}
EOF
end

def format_for(args)
    args.collect do |(type,name)|
      case type
      when "char*", "const char*"
	  "%s"
      when "int", "osync_bool", "unsigned int"
	  "%i"
      else
	  "%p"
      end
   end.join(", ")
end

#
# Parse the signature and returns some info
#
def parse_signature(signature)
    args 	  = signature.sub(/^.*\((.*)\).*$/,"\\1").split(",").
			      collect {|arg| arg.trim }.
			      collect {|arg| match=(/(.*[[:blank:]\*])([_[:alpha:]]+)$/.match(arg)); match[1..2] if match }.
			      collect {|(type,name)| [type.gsub(/[[:blank:]]*\*[[:blank:]]*/,"*").trim,name.trim]}
    arg_type	  = Hash[args.collect {|(type,name)| [name, type]}]
    arg_pos={}
    args.each_index{|i| arg_pos[args[i][1]]=i}
    result_type	  = signature.sub(/^ *(.*) *\(.*$/,"\\1").trim
    [result_type, args, arg_type]
end




def define_callback(setter, signature, argins, logic)
    (result_type, args, arg_type)=parse_signature(signature)

    callback_name = setter.sub(/^osync_(.*)_set_(.*)_func$/,"osync_rubymodule_\\1_\\2")
    rb_setter_name= setter.sub(/^osync_(.*)_func$/,"rb_osync_rubymodule_\\1")
    $ruby_methods[setter]=rb_setter_name

    owner_type 	  = args.first.first.gsub(/\**/,"")

    puts <<EOF
/* This method is the callback wrapper defined by #{setter} inside ruby */
EOF
    define_rubycall callback_name, signature, argins, <<EOF
    VALUE callback = osync_rubymodule_get_data (#{argins.first}, "#{setter}" );
    VALUE ruby_result = rb_funcall2_protected ( callback, "call", #{argins.size}, ruby_args, &ruby_error );
    if ( ruby_error!=0 ) {
        osync_error_set (error, OSYNC_ERROR_GENERIC, "Failed to call #{callback_name} function!\\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
#{logic}
EOF

puts <<EOF

/* This is the ruby method to set a block as a callback in #{setter} */
VALUE #{rb_setter_name}( int argc, VALUE *argv, VALUE self ) {
    #{owner_type} *arg1 = ( #{owner_type} * ) 0 ;
    void *argp1 = 0 ;
    int res1 = 0 ;
    if ( ( argc < 2 ) || ( argc > 2 ) ) {
        rb_raise ( rb_eArgError, "wrong # of arguments(%d for 2)",argc );
        SWIG_fail;
    }
    res1 = SWIG_ConvertPtr ( argv[0], &argp1,SWIGTYPE_p_#{owner_type}, 0 |  0 );
    if ( !SWIG_IsOK ( res1 ) ) {
        SWIG_exception_fail ( SWIG_ArgError ( res1 ), Ruby_Format_TypeError ( "", "#{owner_type}", "#{setter}", 1, argv[0] ) );\
    }
    arg1 = ( #{owner_type} * ) ( argp1 );
    osync_rubymodule_set_data ( argp1, "#{setter}", argv[1] );
    #{setter} ( arg1, #{callback_name} );
    return Qnil;
fail:
    return Qnil;
}

EOF
end


def define_rubycall(func_name, signature, argins, logic)
    (result_type, args, arg_type)=parse_signature(signature)
    has_result    = result_type != "void"
    has_error     = arg_type.include?("error")
    $stderr.puts "Generating '#{func_name}'"

puts <<EOF
/*
 * TODO: desc
 */
#{result_type} #{func_name}_run(#{args.collect{|typename| typename.join(" ")}.join(", ")}) {
    osync_trace ( TRACE_ENTRY, "%s(#{format_for(args)})", __func__, #{args.collect {|(type,name)| name}.join(", ")});
    int ruby_error = 0;
    #{has_result ? "#{result_type} result = (#{result_type})0;" : "/* no result */" }
    #{has_error ? "" : "OSyncError **error = 0;" }
    /* Where ruby arguments lives */
    VALUE ruby_args[#{argins.size}];
#{
    code=[]
    argins.each_index do
      |i|
      type=arg_type[argins[i]]
      case type
      when "char*", "const char*"
        if arg_type.include?("#{argins[i]}size")
	  assign="SWIG_FromCharPtrAndSize (#{argins[i]}, #{argins[i]}size)"
	elsif arg_type.include?("size")
          assign="SWIG_FromCharPtrAndSize (#{argins[i]}, size)"
	else
          assign="SWIG_FromCharPtr(#{argins[i]})"
        end
      when "void*"
        assign="(#{argins[i]}==NULL?Qnil:*(VALUE*)#{argins[i]})"
      when "osync_bool"
        assign="BOOLR(#{argins[i]})"
      else
	assign="SWIG_NewPointerObj( #{argins[i]}, SWIG_TypeQuery(\"#{arg_type[argins[i]]}\"), 0 |  0 )"
      end
      code <<"    ruby_args[#{i}]=#{assign};"
    end
    code.join("\n")
}
    /* MAYBE? call rb_protect passing ruby_args */
    /* now, finally runs the code*/
#{logic}
    osync_trace ( TRACE_EXIT, \"%s:\", __func__);
    goto exit;
error:
    osync_trace ( TRACE_EXIT_ERROR, "%s: %s", __func__, osync_error_print (error) );
exit:
    #{has_error ? "" : "osync_error_unref(error);" }
    #{has_result ? "return result;": "return;"}
}

void #{func_name}_load_and_run() {
    osync_trace ( TRACE_ENTRY, "%s()", __func__);
    #{has_result ? "#{result_type} result = (#{result_type})0;" : "/* no result */" }
    #{result_type}* result_ptr = (#{result_type}*)0;
    int nargs = funcall_data.nargs;
    struct arg_desc *args = funcall_data.args;
    /* loading args */
#{
    code=[]
    args.each_index {|i|
       code<<"    #{args[i][0]} #{args[i][1]} = *((#{args[i][0]}*)args[#{i}].ptr);"
    }
    code.join("\n")
}
    #{has_result ? "result =" : ""} #{func_name}_run(#{args.collect{|(type,name)| name}.join(", ")});
        /* exiting */
    #{has_result ? "result_ptr = malloc(sizeof(#{result_type}*));": ""}
    #{has_result ? "*result_ptr = result;" : ""}
    #{has_result ? "funcall_data.result = result_ptr;": ""}
    osync_trace ( TRACE_EXIT, \"%s:\", __func__);
    return;
}

#{result_type} #{func_name}(#{args.collect{|typename| typename.join(" ")}.join(", ")}) {
    osync_trace ( TRACE_ENTRY, "%s(#{format_for(args)})", __func__, #{args.collect {|(type,name)| name}.join(", ")});
    #{has_result ? "#{result_type} result;" : "/* no result */" }
    struct arg_desc args[#{args.size}];
    /* init ruby, if needed */
    rubymodule_ruby_needed();

    if (is_rubythread()) {
      debug_thread("Called from ruby. No need to worry with locks.\\n");
      /* If we are running inside the rubythread, there is no need to worry*/
      #{has_result ? "result =" : ""} #{func_name}_run(#{args.collect{|(type,name)| name}.join(", ")});
      goto exit;
    }

#{  code=[]
    code << "    /* saving args */"
    args.each_index {|i|
      code << "    args[#{i}].ptr=&#{args[i][1]};"
      code << "    args[#{i}].type=\"#{args[i][0]}\";"
    }
    code.join("\n")
}
    debug_thread("Locking!\\n");
    pthread_mutex_lock ( &ruby_context_lock);
    debug_thread("Waiting for my time!\\n");
    if (!ruby_running) {
      debug_thread("Ruby thread is not running. Waiting\\n");
      pthread_cond_wait(&fcall_ruby_running, &ruby_context_lock);
      debug_thread("Ruby thread is running!\\n");
    }
    /* Check if someone is already using funcall */
    if (funcall_data.func) {
      debug_thread("Someone is already using funcall. Waiting...\\n");
      pthread_cond_wait(&fcall_free, &ruby_context_lock);
      debug_thread("Now funcall is mine!\\n");
    }
    funcall_data.nargs  = #{args.size};
    funcall_data.args 	= args;
    funcall_data.func   = #{func_name}_load_and_run;
    funcall_data.result = NULL;

    debug_thread("Sent!\\n");
    pthread_cond_signal(&fcall_requested);

    debug_thread("Waiting return!\\n");
    pthread_cond_wait(&fcall_returned, &ruby_context_lock);

    debug_thread("Returned!\\n");
    /* freeing */
    if (funcall_data.result) {
      #{has_result ? "result = *(#{result_type}*)funcall_data.result;" : "/* no result */"}
      free(funcall_data.result);
    }
    funcall_data.func = NULL;
    pthread_cond_signal(&fcall_free);
    pthread_mutex_unlock ( &ruby_context_lock);
exit:
    #{has_result ? "return result;" : "return;" }
};

EOF
end






require "date"

puts <<EOF
/*
 * This file was generated by #{__FILE__} at #{DateTime.now}
 *
 */
void rubymodule_ruby_needed();

struct threaded_funcall;
typedef void (* threaded_func) ();
struct arg_desc {
  const char   *type;
  void	       *ptr;
};
struct threaded_funcall {
   threaded_func 	func;
   int 			nargs;
   struct arg_desc	*args;
   void   		*result;
};
static struct threaded_funcall funcall_data = {0, 0, NULL, NULL};

EOF


define_rubycall  "get_sync_info",
		"osync_bool ( OSyncPluginEnv *env, OSyncError **error )",
		%w{env}, <<'EOF'
    int status;
    VALUE ruby_result = rb_protect (rb_get_sync_info, ruby_args[0], &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_sync_info!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( ruby_result ) ) {
        goto error;
    }
    result = TRUE;
EOF



define_rubycall  "get_format_info",
		"osync_bool ( OSyncFormatEnv *env, OSyncError **error )",
		%w{env}, <<'EOF'
    int status;
    VALUE ruby_result = rb_protect (rb_get_format_info, ruby_args[0], &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_format_info!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( ruby_result ) ) {
        goto error;
    }
    result = TRUE;
EOF

define_rubycall  "get_conversion_info",
		"osync_bool ( OSyncFormatEnv *env, OSyncError **error )",
		%w{env}, <<'EOF'
    int status;
    VALUE ruby_result = rb_protect (rb_get_conversion_info, ruby_args[0], &status );
    if ( status!=0 ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to call %s.get_conversion_info!\n%s", RUBY_PLUGIN_CLASS,
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    if ( !RBOOL ( ruby_result ) ) {
        goto error;
    }
    result = TRUE;
EOF



#
# Plugin
#
define_callback "osync_plugin_set_initialize_func",
		"void* (OSyncPlugin *plugin, OSyncPluginInfo *info, OSyncError **error)",
		%w{plugin info}, <<'EOF'
    VALUE *pplugin_data = malloc(sizeof(VALUE));
    *pplugin_data = ruby_result;
    rb_gc_register_address(pplugin_data);
    result = pplugin_data;
EOF
define_callback "osync_plugin_set_finalize_func",
		"void (OSyncPlugin *plugin, void* plugin_data)",
		%w{plugin plugin_data}, <<'EOF'
    if (plugin_data) {
        rb_gc_unregister_address(plugin_data);
	free(plugin_data);
    }
EOF
define_callback "osync_plugin_set_discover_func",
		"osync_bool (OSyncPlugin *plugin, OSyncPluginInfo *info, void* plugin_data, OSyncError **error)",
		%w{plugin info plugin_data}, <<'EOF'
    result = RBOOL(ruby_result);
EOF


#
# Object Format
#
#typedef void * (* OSyncFormatInitializeFunc) (OSyncObjFormat *format, OSyncError **error);
define_callback "osync_objformat_set_initialize_func",
		"void * (OSyncObjFormat *format, OSyncError **error)",
		%w{format}, <<'EOF'
    VALUE *puser_data = malloc(sizeof(VALUE));
    *puser_data = ruby_result;
    rb_gc_register_address(puser_data);
    result = puser_data;
EOF

#typedef osync_bool (* OSyncFormatFinalizeFunc) (OSyncObjFormat *format, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_finalize_func",
		"osync_bool (OSyncObjFormat *format, void *user_data, OSyncError **error)",
		%w{format user_data}, <<'EOF'
    if (user_data) {
        rb_gc_unregister_address(user_data);
	free(user_data);
    }
    result = TRUE;
EOF

# typedef OSyncConvCmpResult (* OSyncFormatCompareFunc) (OSyncObjFormat *format, const char *leftdata, unsigned int leftsize, const char *rightdata, unsigned int rightsize, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_compare_func",
		"OSyncConvCmpResult (OSyncObjFormat *format, const char *leftdata, unsigned int leftdatasize, const char *rightdata, unsigned int rightdatasize, void *user_data, OSyncError **error)",
		%w{format leftdata rightdata user_data}, <<'EOF'
     if ( !IS_FIXNUM ( ruby_result ) ) {
         osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be a FixNum!\n" );
         goto error;
     }
     result = FIX2INT ( result );
EOF

# typedef osync_bool (* OSyncFormatCopyFunc) (OSyncObjFormat *format, const char *input, unsigned int inpsize, char **output, unsigned int *outpsize, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_copy_func",
		"osync_bool (OSyncObjFormat *format, const char *input, unsigned int inputsize, char **output, unsigned int *outputsize, void *user_data, OSyncError **error)",
		%w{format input user_data}, <<'EOF'
    if ( !IS_STRING ( ruby_result ) ) {
	osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be a String!\n" );
	goto error;
    }
    *outputsize = RSTRING_LEN ( ruby_result );
    *output     = malloc(*outputsize);
    memcpy(*output, RSTRING_PTR ( ruby_result ), *outputsize);
    result = TRUE;
EOF

# typedef osync_bool (* OSyncFormatDuplicateFunc) (OSyncObjFormat *format, const char *uid, const char *input, unsigned int insize, char **newuid, char **output, unsigned int *outsize, osync_bool *dirty, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_duplicate_func",
		"osync_bool (OSyncObjFormat *format, const char *uid, const char *input, unsigned int inputsize, char **newuid, char **output, unsigned int *outputsize, osync_bool *dirty, void *user_data, OSyncError **error)",
		%w{format uid input user_data}, <<'EOF'
    if ( !IS_ARRAY ( ruby_result ) || ( RARRAY_LEN ( ruby_result ) != 3 ) ||
            !IS_STRING ( rb_ary_entry ( ruby_result, 0 ) ) ||
            !IS_STRING ( rb_ary_entry ( ruby_result, 1 ) ) ||
            !IS_BOOL ( rb_ary_entry ( ruby_result, 2 ) )
       ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be an Array with [newuid:string, output:string, dirty:bool] !\n" );
        goto error;
    }
    *newuid 	= strdup(RSTRING_PTR ( rb_ary_entry ( ruby_result, 0 ) ));
    *outputsize = RSTRING_LEN ( ruby_result );
    *output     = malloc(*outputsize);
    memcpy(*output, RSTRING_PTR ( ruby_result ), *outputsize);
    *dirty  	= RBOOL ( rb_ary_entry ( ruby_result, 2 ) );
    result 	= TRUE;
EOF

# typedef osync_bool (* OSyncFormatCreateFunc) (OSyncObjFormat *format, char **data, unsigned int *size, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_create_func",
		"osync_bool (OSyncObjFormat *format, char **data, unsigned int *size, void *user_data, OSyncError **error)",
		%w{format user_data}, <<'EOF'
    if ( !IS_STRING ( ruby_result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be a String!\n" );
        goto error;
    }
    *size = RSTRING_LEN ( ruby_result );
    *data     = malloc(*size);
    memcpy(*data, RSTRING_PTR ( ruby_result ), *size);
    result = TRUE;
EOF

# typedef osync_bool (* OSyncFormatDestroyFunc) (OSyncObjFormat *format, char *data, unsigned int size, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_destroy_func",
		"osync_bool (OSyncObjFormat *format, char *data, unsigned int size, void *user_data, OSyncError **error)",
		%w{format data user_data}, <<'EOF'
    result = RBOOL ( ruby_result );
EOF

# typedef char *(* OSyncFormatPrintFunc) (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_print_func",
		"char * (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)",
		%w{format data user_data}, <<'EOF'
    if ( !IS_STRING ( ruby_result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be a String!\n" );
        goto error;
    }
    result = RSTRING_PTR ( ruby_result );
EOF

# typedef time_t (* OSyncFormatRevisionFunc) (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_revision_func",
		"time_t (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)",
		%w{format data user_data}, <<'EOF'
    ruby_result = rb_funcall2_protected ( ruby_result, "to_i", 0, NULL, &ruby_error );
    if ( ( ruby_error=0 ) || !IS_FIXNUM ( ruby_result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "Failed to convert time to a number!\n%s",
                          osync_rubymodule_error_bt ( __FILE__, __func__,__LINE__ ) );
        goto error;
    }
    result = FIX2LONG ( ruby_result );
EOF

# typedef osync_bool (* OSyncFormatMarshalFunc) (OSyncObjFormat *format, const char *input, unsigned int inputsize, OSyncMarshal *marshal, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_marshal_func",
		"osync_bool (OSyncObjFormat *format, const char *input, unsigned int inputsize, OSyncMarshal *marshal, void *user_data, OSyncError **error)",
		%w{format input marshal user_data}, <<'EOF'
    result = RBOOL ( ruby_result );
EOF

# typedef osync_bool (* OSyncFormatDemarshalFunc) (OSyncObjFormat *format, OSyncMarshal *marshal, char **output, unsigned int *outputsize, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_demarshal_func",
		"osync_bool (OSyncObjFormat *format, OSyncMarshal *marshal, char **output, unsigned int *outputsize, void *user_data, OSyncError **error)",
		%w{format marshal user_data}, <<'EOF'
    if ( !IS_STRING ( ruby_result ) ) {
        osync_error_set ( error, OSYNC_ERROR_GENERIC, "The result should be a String!\n" );
        goto error;
    }
    *outputsize = RSTRING_LEN ( ruby_result );
    *output     = malloc(*outputsize);
    memcpy(*output, RSTRING_PTR ( ruby_result ), *outputsize);
    result 	= TRUE;
EOF

# typedef osync_bool (* OSyncFormatValidateFunc) (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error);
define_callback "osync_objformat_set_validate_func",
		"osync_bool (OSyncObjFormat *format, const char *data, unsigned int size, void *user_data, OSyncError **error)",
		%w{format data user_data}, <<'EOF'
    result = RBOOL ( ruby_result );
EOF


#
# Sink
#
# typedef void (* OSyncSinkConnectFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data);
define_callback "osync_objtype_sink_set_connect_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data)",
		%w{sink info ctx data}, <<'EOF'
EOF

#typedef void (* OSyncSinkDisconnectFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data);
define_callback "osync_objtype_sink_set_disconnect_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data)",
		%w{sink info ctx data}, <<'EOF'
EOF

# typedef void (* OSyncSinkGetChangesFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data);
define_callback "osync_objtype_sink_set_get_changes_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data)",
		%w{sink info ctx slow_sync data}, <<'EOF'
EOF

# typedef void (* OSyncSinkCommitFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data);
define_callback "osync_objtype_sink_set_commit_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data)",
		%w{sink info ctx change data}, <<'EOF'
EOF

# typedef void (* OSyncSinkCommittedAllFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data);
define_callback "osync_objtype_sink_set_committed_all_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data)",
		%w{sink info ctx data}, <<'EOF'
EOF

# typedef void (* OSyncSinkReadFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data);
define_callback "osync_objtype_sink_set_read_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, OSyncChange *change, void *data)",
		%w{sink info ctx change data}, <<'EOF'
EOF

# typedef void (* OSyncSinkSyncDoneFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data);
define_callback "osync_objtype_sink_set_sync_done_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, void *data)",
		%w{sink info ctx data}, <<'EOF'
EOF

# typedef void (* OSyncSinkConnectDoneFn) (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data);
define_callback "osync_objtype_sink_set_connect_done_func",
		"void (OSyncObjTypeSink *sink, OSyncPluginInfo *info, OSyncContext *ctx, osync_bool slow_sync, void *data)",
		%w{sink info ctx slow_sync data}, <<'EOF'
EOF

define_Init_rubymodule_callbacks