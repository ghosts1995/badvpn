/**
 * @file call2.c
 * @author Ambroz Bizjak <ambrop7@gmail.com>
 * 
 * @section LICENSE
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the
 *    names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * @section DESCRIPTION
 * 
 * 
 * Synopsis:
 *   call(string template, list args)
 * 
 * Description:
 *   Calls a process template. The 'template' argument is the name of the process
 *   template to call, and the 'list' argument is a list of arguments for the
 *   process template. Calling a process template is roughly equivalent to placing
 *   the statements within that template into the place of call(), except for the
 *   points presented next. The 'template' argument can be a special value "<none>",
 *   which makes call() a no-op.
 * 
 *   The process created from the called template will be able to access the arguments
 *   that were given in the 'args' argument to call() via the '_argN' predefined\
 *   objects (e.g. _arg0 for the first argumens), and also via '_args' for the entire
 *   argument list.
 *
 *   The called process also will be able to access objects within the calling
 *   process as seen by the call() statement. However such any access needs to happen
 *   via a special '_caller' predefined object. For example, if there is a statement
 *   'var("a") x;' somewhere above the call() statement, the called process can access
 *   it as '_caller.x'.
 * 
 *   Note that call() preserves backtracking semantics, i.e. when a statement within
 *   the called process goes down after having gone up, the behaviour really is as
 *   if the call() statement was replaced with the statements in the called template,
 *   (disregarding variable resolution).
 * 
 *   Because the template name is an argument, call() can be used for branching.
 *   For example, if we have an object 'x' with the value "true" or "false", a
 *   branch can be performed by defining two process templates, 'branch_true'
 *   and 'branch_false', and branching with the following code:
 * 
 *     concat("branch_", x) name;
 *     call(name, {});
 * 
 * 
 * Synopsis:
 *   call_with_caller_target(string template, list args, string caller_target)
 * 
 * Description:
 *   Like call(), except that the target of the '_caller' predefined object is
 *   specified by the 'caller_target' argument. This is indented to be used from
 *   generic code for user-specified callbacks, allowing the user to easily refer to
 *   his own objects from inside the callback.
 * 
 *   The 'caller_target' must be a non-empty string referring to an actual object;
 *   there is no choice of 'caller_target' that would make call_with_caller_target()
 *   equivalent to call().
 * 
 * 
 * Synopsis:
 *   embcall2_multif(string cond1, string template1, ..., [string else_template])
 * 
 * Description:
 *   This is an internal command used to implement the 'If' clause. The arguments
 *   are pairs of (cond, template), where 'cond' is a condition in form of a string,
 *   and 'template' is the name of the process template for this condition. The
 *   template corresponding to the first condition equal to "true" is called; if
 *   there is no true condition, either the template 'else_template' is called,
 *   if it is provided, or nothing is performed, if 'else_template' is not provided.
 * 
 * 
 * Synopsis:
 *   inline_code(string template)
 *   inline_code::call(list args)
 * 
 * Description:
 *   The inline_code() acts as a proxy object for calling the specified template.
 *   The inline_code::call calls the template of the corresponding inline_code
 *   instance, in a manner similar to a simple "call". The called template will
 *   have access to the following resources:
 *   - The arguments will be available in the usual fashion.
 *   - The scope of the inline_code::call instance will be available as "_caller".
 *   - The scope of the inline_code instance will be directly available.
 *   - The scope of the inline_code instance will also be available as "_scope".
 *     This is useful to access shadowed names, e.g. "_caller" and the arguments
 *     stuff (_argN, _args).
 */

#include <stdlib.h>
#include <string.h>

#include <misc/debug.h>
#include <misc/offset.h>
#include <structure/LinkedList0.h>

#include <ncd/module_common.h>

#include <generated/blog_channel_ncd_call2.h>

#define STATE_WORKING 1
#define STATE_UP 2
#define STATE_WAITING 3
#define STATE_TERMINATING 4
#define STATE_NONE 5

#define NUM_STATIC_NAMES 4

struct instance;

typedef void (*call_extra_free_cb) (struct instance *o);

struct instance {
    NCDModuleInst *i;
    call_extra_free_cb extra_free_cb;
    NCDModuleProcess process;
    int state;
};

struct instance_with_caller_target {
    struct instance base;
    NCD_string_id_t *dynamic_names;
    size_t num_names;
    NCD_string_id_t static_names[NUM_STATIC_NAMES];
};

struct inline_code {
    NCDModuleInst *i;
    NCDValRef template_name;
    LinkedList0 calls_list;
};

struct inline_code_call {
    struct instance base;
    struct inline_code *ic;
    LinkedList0Node ic_node;
};

#define NAMES_PARAM_NAME CallNames
#define NAMES_PARAM_TYPE struct instance_with_caller_target
#define NAMES_PARAM_MEMBER_DYNAMIC_NAMES dynamic_names
#define NAMES_PARAM_MEMBER_STATIC_NAMES static_names
#define NAMES_PARAM_MEMBER_NUM_NAMES num_names
#define NAMES_PARAM_NUM_STATIC_NAMES NUM_STATIC_NAMES
#include <ncd/extra/make_fast_names.h>

static void process_handler_event (NCDModuleProcess *process, int event);
static int process_func_getspecialobj_embed (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object);
static int process_func_getspecialobj_noembed (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object);
static int process_func_getspecialobj_with_caller_target (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object);
static int caller_obj_func_getobj (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object);
static int caller_obj_func_getobj_with_caller_target (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object);
static int func_new_templ (void *vo, NCDModuleInst *i, NCDValRef template_name, NCDValRef args, NCDModuleProcess_func_getspecialobj func_getspecialobj, call_extra_free_cb extra_free_cb);
static void instance_free (struct instance *o);
static void call_with_caller_target_extra_free (struct instance *bo);
static void inline_code_extra_free (struct instance *bo);
static int inline_code_call_process_getspecialobj (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object);
static int inline_code_scope_obj_getobj (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object);

static void process_handler_event (NCDModuleProcess *process, int event)
{
    struct instance *o = UPPER_OBJECT(process, struct instance, process);
    
    switch (event) {
        case NCDMODULEPROCESS_EVENT_UP: {
            ASSERT(o->state == STATE_WORKING)
            
            // signal up
            NCDModuleInst_Backend_Up(o->i);
            
            // set state up
            o->state = STATE_UP;
        } break;
        
        case NCDMODULEPROCESS_EVENT_DOWN: {
            ASSERT(o->state == STATE_UP)
            
            // signal down
            NCDModuleInst_Backend_Down(o->i);
            
            // set state waiting
            o->state = STATE_WAITING;
        } break;
        
        case NCDMODULEPROCESS_EVENT_TERMINATED: {
            ASSERT(o->state == STATE_TERMINATING)
            
            // die finally
            instance_free(o);
            return;
        } break;
        
        default: ASSERT(0);
    }
}

static int process_func_getspecialobj_embed (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance *o = UPPER_OBJECT(process, struct instance, process);
    
    return NCDModuleInst_Backend_GetObj(o->i, name, out_object);
}

static int process_func_getspecialobj_noembed (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance *o = UPPER_OBJECT(process, struct instance, process);
    
    if (name == NCD_STRING_CALLER) {
        *out_object = NCDObject_Build(-1, o, NCDObject_no_getvar, caller_obj_func_getobj);
        return 1;
    }
    
    return 0;
}

static int process_func_getspecialobj_with_caller_target (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance *o = UPPER_OBJECT(process, struct instance, process);
    
    if (name == NCD_STRING_CALLER) {
        *out_object = NCDObject_Build(-1, o, NCDObject_no_getvar, caller_obj_func_getobj_with_caller_target);
        return 1;
    }
    
    return 0;
}

static int caller_obj_func_getobj (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance *o = NCDObject_DataPtr(obj);
    
    return NCDModuleInst_Backend_GetObj(o->i, name, out_object);
}

static int caller_obj_func_getobj_with_caller_target (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance_with_caller_target *o_ch = NCDObject_DataPtr(obj);
    ASSERT(o_ch->num_names > 0)
    
    NCD_string_id_t *names = CallNames_GetNames(o_ch);
    
    NCDObject object;
    if (!NCDModuleInst_Backend_GetObj(o_ch->base.i, names[0], &object)) {
        return 0;
    }
    
    NCDObject obj2;
    if (!NCDObject_ResolveObjExprCompact(&object, names + 1, o_ch->num_names - 1, &obj2)) {
        return 0;
    }
    
    if (name == NCD_STRING_EMPTY) {
        *out_object = obj2;
        return 1;
    }
    
    return NCDObject_GetObj(&obj2, name, out_object);
}

static int func_new_templ (void *vo, NCDModuleInst *i, NCDValRef template_name, NCDValRef args, NCDModuleProcess_func_getspecialobj func_getspecialobj, call_extra_free_cb extra_free_cb)
{
    ASSERT(NCDVal_IsInvalid(template_name) || NCDVal_IsString(template_name))
    ASSERT(NCDVal_IsInvalid(args) || NCDVal_IsList(args))
    ASSERT(func_getspecialobj)
    
    struct instance *o = vo;
    o->i = i;
    o->extra_free_cb = extra_free_cb;
    
    if (NCDVal_IsInvalid(template_name) || ncd_is_none(template_name)) {
        // signal up
        NCDModuleInst_Backend_Up(o->i);
        
        // set state none
        o->state = STATE_NONE;
    } else {
        // create process
        if (!NCDModuleProcess_InitValue(&o->process, o->i, template_name, args, process_handler_event)) {
            ModuleLog(o->i, BLOG_ERROR, "NCDModuleProcess_Init failed");
            goto fail0;
        }
        
        // set special functions
        NCDModuleProcess_SetSpecialFuncs(&o->process, func_getspecialobj);
        
        // set state working
        o->state = STATE_WORKING;
    }
    
    return 1;
    
fail0:
    NCDModuleInst_Backend_DeadError(i);
    return 0;
}

static void instance_free (struct instance *o)
{
    if (o->extra_free_cb) {
        o->extra_free_cb(o);
    }
    
    // free process
    if (o->state != STATE_NONE) {
        NCDModuleProcess_Free(&o->process);
    }
    
    NCDModuleInst_Backend_Dead(o->i);
}

static void func_new_call (void *vo, NCDModuleInst *i, const struct NCDModuleInst_new_params *params)
{
    NCDValRef template_arg;
    NCDValRef args_arg;
    if (!NCDVal_ListRead(params->args, 2, &template_arg, &args_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong arity");
        goto fail0;
    }
    if (!NCDVal_IsString(template_arg) || !NCDVal_IsList(args_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong type");
        goto fail0;
    }
    
    func_new_templ(vo, i, template_arg, args_arg, process_func_getspecialobj_noembed, NULL);
    return;
    
fail0:
    NCDModuleInst_Backend_DeadError(i);
}

static void func_new_call_with_caller_target (void *vo, NCDModuleInst *i, const struct NCDModuleInst_new_params *params)
{
    struct instance *o = vo;
    struct instance_with_caller_target *o_ct = vo;
    o->i = i;
    o->extra_free_cb = call_with_caller_target_extra_free;
    
    NCDValRef template_arg;
    NCDValRef args_arg;
    NCDValRef caller_target_arg;
    if (!NCDVal_ListRead(params->args, 3, &template_arg, &args_arg, &caller_target_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong arity");
        goto fail0;
    }
    if (!NCDVal_IsString(template_arg) || !NCDVal_IsList(args_arg) || !NCDVal_IsString(caller_target_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong type");
        goto fail0;
    }
    
    NCDValContString cts;
    if (!NCDVal_StringContinuize(caller_target_arg, &cts)) {
        ModuleLog(i, BLOG_ERROR, "NCDVal_StringContinuize failed");
        goto fail0;
    }
    
    int res = CallNames_InitNames(o_ct, i->params->iparams->string_index, cts.data, NCDVal_StringLength(caller_target_arg));
    NCDValContString_Free(&cts);
    if (!res) {
        ModuleLog(i, BLOG_ERROR, "CallerNames_InitNames failed");
        goto fail0;
    }
    
    if (ncd_is_none(template_arg)) {
        // signal up
        NCDModuleInst_Backend_Up(i);
        
        // set state none
        o->state = STATE_NONE;
    } else {
        // create process
        if (!NCDModuleProcess_InitValue(&o->process, i, template_arg, args_arg, process_handler_event)) {
            ModuleLog(i, BLOG_ERROR, "NCDModuleProcess_Init failed");
            goto fail1;
        }
        
        // set special functions
        NCDModuleProcess_SetSpecialFuncs(&o->process, process_func_getspecialobj_with_caller_target);
        
        // set state working
        o->state = STATE_WORKING;
    }
    
    return;
    
fail1:
    CallNames_FreeNames(o_ct);
fail0:
    NCDModuleInst_Backend_DeadError(i);
}

static void call_with_caller_target_extra_free (struct instance *bo)
{
    struct instance_with_caller_target *o = (void *)bo;
    
    CallNames_FreeNames(o);
}

static void func_new_embcall_multif (void *vo, NCDModuleInst *i, const struct NCDModuleInst_new_params *params)
{
    NCDValRef args = params->args;
    
    NCDValRef template_value = NCDVal_NewInvalid();
    
    size_t count = NCDVal_ListCount(args);
    size_t j = 0;
    
    while (j < count) {
        NCDValRef arg = NCDVal_ListGet(args, j);
        
        if (j == count - 1) {
            if (!NCDVal_IsString(arg)) {
                ModuleLog(i, BLOG_ERROR, "bad arguments");
                goto fail0;
            }
            
            template_value = arg;
            break;
        }
        
        NCDValRef arg2 = NCDVal_ListGet(args, j + 1);
        
        int arg_val;
        if (!ncd_read_boolean(arg, &arg_val) || !NCDVal_IsString(arg2)) {
            ModuleLog(i, BLOG_ERROR, "bad arguments");
            goto fail0;
        }
        
        if (arg_val) {
            template_value = arg2;
            break;
        }
        
        j += 2;
    }
    
    func_new_templ(vo, i, template_value, NCDVal_NewInvalid(), process_func_getspecialobj_embed, NULL);
    return;
    
fail0:
    NCDModuleInst_Backend_DeadError(i);
}

static void func_die (void *vo)
{
    struct instance *o = vo;
    ASSERT(o->state != STATE_TERMINATING)
    
    // if none, die now
    if (o->state == STATE_NONE) {
        instance_free(o);
        return;
    }
    
    // request process to terminate
    NCDModuleProcess_Terminate(&o->process);
    
    // set state terminating
    o->state = STATE_TERMINATING;
}

static void func_clean (void *vo)
{
    struct instance *o = vo;
    if (o->state != STATE_WAITING) {
        return;
    }
    
    // allow process to continue
    NCDModuleProcess_Continue(&o->process);
    
    // set state working
    o->state = STATE_WORKING;
}

static int func_getobj (void *vo, NCD_string_id_t name, NCDObject *out_object)
{
    struct instance *o = vo;
    
    if (o->state == STATE_NONE) {
        return 0;
    }
    
    return NCDModuleProcess_GetObj(&o->process, name, out_object);
}

static void inline_code_new (void *vo, NCDModuleInst *i, const struct NCDModuleInst_new_params *params)
{
    NCDValRef template_arg;
    if (!NCDVal_ListRead(params->args, 1, &template_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong arity");
        goto fail0;
    }
    if (!NCDVal_IsString(template_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong type");
        goto fail0;
    }
    
    struct inline_code *o = vo;
    o->i = i;
    o->template_name = template_arg;
    LinkedList0_Init(&o->calls_list);
    
    NCDModuleInst_Backend_Up(i);
    return;
    
fail0:
    NCDModuleInst_Backend_DeadError(i);
}

static void inline_code_die (void *vo)
{
    struct inline_code *o = vo;
    
    for (LinkedList0Node *ln = LinkedList0_GetFirst(&o->calls_list); ln; ln = LinkedList0Node_Next(ln)) {
        struct inline_code_call *call = UPPER_OBJECT(ln, struct inline_code_call, ic_node);
        ASSERT(call->ic == o)
        call->ic = NULL;
    }
    
    NCDModuleInst_Backend_Dead(o->i);
}

static void inline_code_call_new (void *vo, NCDModuleInst *i, const struct NCDModuleInst_new_params *params)
{
    NCDValRef args_arg;
    if (!NCDVal_ListRead(params->args, 1, &args_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong arity");
        goto fail0;
    }
    if (!NCDVal_IsList(args_arg)) {
        ModuleLog(i, BLOG_ERROR, "wrong type");
        goto fail0;
    }
    
    struct inline_code_call *o = vo;
    struct inline_code *ic = NCDModuleInst_Backend_GetUser((NCDModuleInst *)params->method_user);
    
    if (func_new_templ(vo, i, ic->template_name, args_arg, inline_code_call_process_getspecialobj, inline_code_extra_free)) {
        o->ic = ic;
        LinkedList0_Prepend(&ic->calls_list, &o->ic_node);
    }
    
    return;
    
fail0:
    NCDModuleInst_Backend_DeadError(i);
}

static void inline_code_extra_free (struct instance *bo)
{
    struct inline_code_call *o = (void *)bo;
    
    if (o->ic) {
        LinkedList0_Remove(&o->ic->calls_list, &o->ic_node);
    }
}

static int inline_code_call_process_getspecialobj (NCDModuleProcess *process, NCD_string_id_t name, NCDObject *out_object)
{
    struct inline_code_call *o = UPPER_OBJECT(process, struct inline_code_call, base.process);
    
    if (name == NCD_STRING_CALLER) {
        *out_object = NCDObject_Build(-1, o, NCDObject_no_getvar, caller_obj_func_getobj);
        return 1;
    }
    
    if (!o->ic) {
        ModuleLog(o->base.i, BLOG_ERROR, "inline_code object is gone");
        return 0;
    }
    
    if (name == NCD_STRING_SCOPE) {
        *out_object = NCDObject_Build(-1, o->ic, NCDObject_no_getvar, inline_code_scope_obj_getobj);
        return 1;
    }
    
    return NCDModuleInst_Backend_GetObj(o->ic->i, name, out_object);
}

static int inline_code_scope_obj_getobj (const NCDObject *obj, NCD_string_id_t name, NCDObject *out_object)
{
    struct inline_code *ic = NCDObject_DataPtr(obj);
    
    return NCDModuleInst_Backend_GetObj(ic->i, name, out_object);
}

static struct NCDModule modules[] = {
    {
        .type = "call",
        .func_new2 = func_new_call,
        .func_die = func_die,
        .func_clean = func_clean,
        .func_getobj = func_getobj,
        .flags = NCDMODULE_FLAG_CAN_RESOLVE_WHEN_DOWN|NCDMODULE_FLAG_ACCEPT_NON_CONTINUOUS_STRINGS,
        .alloc_size = sizeof(struct instance)
    }, {
        .type = "call_with_caller_target",
        .func_new2 = func_new_call_with_caller_target,
        .func_die = func_die,
        .func_clean = func_clean,
        .func_getobj = func_getobj,
        .flags = NCDMODULE_FLAG_CAN_RESOLVE_WHEN_DOWN|NCDMODULE_FLAG_ACCEPT_NON_CONTINUOUS_STRINGS,
        .alloc_size = sizeof(struct instance_with_caller_target)
    }, {
        .type = "embcall2_multif",
        .func_new2 = func_new_embcall_multif,
        .func_die = func_die,
        .func_clean = func_clean,
        .func_getobj = func_getobj,
        .flags = NCDMODULE_FLAG_CAN_RESOLVE_WHEN_DOWN|NCDMODULE_FLAG_ACCEPT_NON_CONTINUOUS_STRINGS,
        .alloc_size = sizeof(struct instance)
    }, {
        .type = "inline_code",
        .func_new2 = inline_code_new,
        .func_die = inline_code_die,
        .alloc_size = sizeof(struct inline_code)
    }, {
        .type = "inline_code::call",
        .func_new2 = inline_code_call_new,
        .func_die = func_die,
        .func_clean = func_clean,
        .func_getobj = func_getobj,
        .flags = NCDMODULE_FLAG_CAN_RESOLVE_WHEN_DOWN|NCDMODULE_FLAG_ACCEPT_NON_CONTINUOUS_STRINGS,
        .alloc_size = sizeof(struct inline_code_call)
    }, {
        .type = NULL
    }
};

const struct NCDModuleGroup ncdmodule_call2 = {
    .modules = modules
};
