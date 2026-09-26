#include <stdio.h>
#include <Python.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <wordexp.h>
#include <unistd.h>

#include "dlsym-intercept.h"
#include "moduleedit.h"

#ifdef DEBUG
#define IFDEBUG
#else
#define IFDEBUG if (0)
#endif


static void parse_parameter_str(void);


static PyObject*(*real_PyUnicode_FromFormatV)(const char* format, va_list vargs) = NULL;
static PyObject*(*real_PyUnicode_AsUTF8String)(PyObject* unicode) = NULL;
static char*(*real_PyBytes_AsString)(PyObject* bytes) = NULL;
static Py_ssize_t (*real_PyBytes_Size)(PyObject *o) = NULL;
static void(*real_Py_DecRef)(PyObject *o) = NULL;
static PyObject*(*real_PyObject_Str)(PyObject *o) = NULL;
static PyObject*(*real_PyCode_GetCode)(PyCodeObject *co) = NULL;
static PyObject*(*real_PyObject_GetAttrString)(PyObject *o, const char *attr_name) = NULL;
static int (*real_PyBytes_AsStringAndSize)(PyObject *obj, char **buffer, Py_ssize_t *length) = NULL;
static PyObject* (*real_Py_CompileString)(const char *str, const char *filename, int start) = NULL;
static void (*real_PyErr_Print)(void) = NULL;
static void (*real_PyErr_Clear)(void) = NULL;
static PyObject* (*real_PyRun_String)(const char *str, int start, PyObject *globals, PyObject *locals) = NULL;


// hooked functions
static PyObject* my_PyUnicode_FromString(const char *str);
static PyObject* my_PyUnicode_FromFormat(const char *format, ...);
static PyStatus my_PyConfig_SetBytesString(PyConfig *config, wchar_t *const *config_str, const char *str);
static PyStatus my_Py_InitializeFromConfig(const PyConfig *config);
static PyObject* my_PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals);
static PyObject* my_PyImport_AddModule(const char* name);
static PyStatus my_PyConfig_SetWideStringList(PyConfig *config, PyWideStringList *list, Py_ssize_t length, wchar_t **items);
static PyObject* my_PyImport_ExecCodeModule(const char *name, PyObject *co);
static int my_PyObject_SetAttrString(PyObject *o, const char *attr_name, PyObject *v);
static int my_PySys_SetObject(const char *name, PyObject *v);
static int my_PyRun_SimpleStringFlags(const char *command, PyCompilerFlags* flags);
static PyObject* my_PyModule_GetDict(PyObject* module);
static PyObject* my_PyUnicode_DecodeFSDefault(const char* str);



void interceptor_init()
{
    interceptor_register_hook("PyUnicode_FromString", my_PyUnicode_FromString);
    interceptor_register_hook("PyUnicode_FromFormat", my_PyUnicode_FromFormat);
    interceptor_register_hook("PyConfig_SetBytesString", my_PyConfig_SetBytesString);
    interceptor_register_hook("Py_InitializeFromConfig", my_Py_InitializeFromConfig);
    interceptor_register_hook("PyEval_EvalCode", my_PyEval_EvalCode);
    interceptor_register_hook("PyImport_AddModule", my_PyImport_AddModule);
    interceptor_register_hook("PyConfig_SetWideStringList", my_PyConfig_SetWideStringList);
    interceptor_register_hook("PyImport_ExecCodeModule", my_PyImport_ExecCodeModule);
    interceptor_register_hook("PyObject_SetAttrString", my_PyObject_SetAttrString);
    interceptor_register_hook("PySys_SetObject", my_PySys_SetObject);
    interceptor_register_hook("PyRun_SimpleStringFlags", my_PyRun_SimpleStringFlags);
    interceptor_register_hook("PyModule_GetDict", my_PyModule_GetDict);
    interceptor_register_hook("PyUnicode_DecodeFSDefault", my_PyUnicode_DecodeFSDefault);
}



void interceptor_dlopen(const char* path, int flags, void* handle)
{
    (void)flags;
    parse_parameter_str();
    if (handle && path && strstr(path, "libpython3"))
    {
        real_PyBytes_AsString = real_dlsym(handle, "PyBytes_AsString");
        real_PyBytes_AsStringAndSize = real_dlsym(handle, "PyBytes_AsStringAndSize");
        real_PyBytes_Size = real_dlsym(handle, "PyBytes_Size");
        real_PyCode_GetCode = real_dlsym(handle, "PyCode_GetCode");
        real_Py_CompileString = real_dlsym(handle, "Py_CompileString");
        real_Py_DecRef = real_dlsym(handle, "Py_DecRef");
        real_PyErr_Clear = real_dlsym(handle, "PyErr_Clear");
        real_PyErr_Print = real_dlsym(handle, "PyErr_Print");
        real_PyObject_GetAttrString = real_dlsym(handle, "PyObject_GetAttrString");
        real_PyObject_Str = real_dlsym(handle, "PyObject_Str");
        real_PyRun_String = real_dlsym(handle, "PyRun_String");
        real_PyUnicode_AsUTF8String = real_dlsym(handle, "PyUnicode_AsUTF8String");
        real_PyUnicode_FromFormatV = real_dlsym(handle, "PyUnicode_FromFormatV");
    }
}



typedef struct {
    char* script_name;
    unsigned int print_imports:1;
    unsigned int parameter_str_parsed:1;
} opts_t;



static opts_t options = {
    .script_name = NULL,
    .print_imports = 0,
    .parameter_str_parsed = 0,
};



static void print_help(int exit_code)
{
    puts(
            "help"
    );
    exit(exit_code);
}



void malloc_and_strcpy(char** dest, char* src)
{
    *dest = malloc(strlen(src) + 1);
    if (*dest)
    {
        strcpy(*dest, src);
    }
    else
    {
        exit(EXIT_FAILURE);
    }
}



static void parse_parameter(int argc, char** argv)
{
    int opt = 0;
    while ((opt = getopt(argc, argv, "ps:h")) != -1)
    {
        switch (opt)
        {
            case 'p':
                options.print_imports = 1;
                break;
            case 's':
                if (!options.script_name)
                {
                    malloc_and_strcpy(&options.script_name, optarg);
                }
                break;
            case 'h':
                print_help(EXIT_SUCCESS);
                break;
            default:
                printf("error parameters\n");
                print_help(EXIT_FAILURE);
                break;
        }
    }
}



static void parse_parameter_str()
{
    if (!options.parameter_str_parsed) // "lazy init", because musl libc wordexp is not ready at libstart
    {
        options.parameter_str_parsed = 1;
        const char ENV_VAR[] = "INJECT_PARAMETER";
        const char* parameter_str = getenv(ENV_VAR);
        IFDEBUG printf("%s: '%s'\n", ENV_VAR, parameter_str);
        if (parameter_str)
        {
            wordexp_t p;
            p.we_offs = 1; // insert dummy entry for getopts
            int res = wordexp(parameter_str, &p, WRDE_NOCMD | WRDE_DOOFFS);
            if (res == 0)
            {
                parse_parameter(p.we_wordc + 1, p.we_wordv);
                wordfree(&p);
                return;
            }
            else if (res == WRDE_BADCHAR)
            {
                dprintf(STDERR_FILENO, "%s: Illegal occurrence of newline or one of |, &, ;, <, >, (, ), {, }.\n", ENV_VAR);
            }
            else if (res == WRDE_SYNTAX)
            {
                dprintf(STDERR_FILENO, "%s: Syntax error, such as unbalanced parentheses or unmatched quotes.\n", ENV_VAR);
            }
            else
            {
                char* err_val = NULL;
                #define X(name) if(res == name) err_val = #name;
                    X(WRDE_BADVAL) X(WRDE_CMDSUB) X(WRDE_NOSPACE)
                #undef X
                dprintf(STDERR_FILENO, "%s: %s\n", ENV_VAR, err_val);
            }
            exit(EXIT_FAILURE);
        }
    }
}



static PyObject* my_PyUnicode_FromString(const char *str)
{
    IFDEBUG printf("-> PyUnicode_FromString: %s\n", str);
    PyObject*(*func)(const char *str) = interceptor_get_original_addr("PyUnicode_FromString");
    return  func(str);
}



static PyObject* my_PyUnicode_FromFormat(const char *format, ...)
{
    va_list vargp;
    va_start(vargp, format);
    PyObject* unicode_o = real_PyUnicode_FromFormatV(format, vargp);
    va_end(vargp);
    PyObject* bytes_o = real_PyUnicode_AsUTF8String(unicode_o);
    const char* res_str = real_PyBytes_AsString(bytes_o);
    IFDEBUG printf("-> PyUnicode_FromFormat(\"%s\", ...) -> %s\n", format, res_str);
    real_Py_DecRef(bytes_o);
    return unicode_o;
}



static PyStatus my_PyConfig_SetBytesString(PyConfig *config, wchar_t *const *config_str, const char *str)
{
    IFDEBUG printf("-> PyConfig_SetBytesString(%p, \"%ls\", \"%s\")\n", config, *config_str, str);
    PyStatus(*func)(PyConfig*, wchar_t *const *, const char *) = interceptor_get_original_addr("PyConfig_SetBytesString");
    return func(config, config_str, str);
}



static PyStatus my_Py_InitializeFromConfig(const PyConfig *config)
{
    IFDEBUG printf("-> Py_InitializeFromConfig(%p)\n", config);
    if (config->home)
    {
        IFDEBUG printf("home: %ls\n", config->home);
    }
    if (config->platlibdir)
    {
        IFDEBUG printf("platlibdir: %ls\n", config->platlibdir);
    }
    if (config->pythonpath_env)
    {
        IFDEBUG printf("pythonpath_env: %ls\n", config->pythonpath_env);
    }
    if (config->prefix)
    {
        IFDEBUG printf("prefix: %ls\n", config->prefix);
    }
    if (config->program_name)
    {
        IFDEBUG printf("program_name: %ls\n", config->program_name);
    }
    if (config->run_command)
    {
        IFDEBUG printf("run_command: %ls\n", config->run_command);
    }
    if (config->run_filename)
    {
        IFDEBUG printf("run_filename: %ls\n", config->run_filename);
    }
    if (config->run_module)
    {
        IFDEBUG printf("run_module: %ls\n", config->run_module);
    }

    PyStatus(*func)(const PyConfig*) = interceptor_get_original_addr("Py_InitializeFromConfig");
    return func(config);
}


static void to_str_pyobject(const char* format, PyObject *o)
{
    PyObject* unicode_o = real_PyObject_Str(o);
    PyObject* bytes_o = real_PyUnicode_AsUTF8String(unicode_o);
    const char* res_str = real_PyBytes_AsString(bytes_o);
    IFDEBUG printf(format, res_str);
    real_Py_DecRef(bytes_o);
    real_Py_DecRef(unicode_o);
    return;
}


static int to_str_pyobject_buffer(char* buffer, size_t buffer_size, PyObject *o)
{
    PyObject* bytes_o = real_PyUnicode_AsUTF8String(o);
    const char* res_str = real_PyBytes_AsString(bytes_o);
    strncpy(buffer, res_str, buffer_size);
    real_Py_DecRef(bytes_o);
    return 1;
}


static int my_PyEval_EvalCode_inject_next = 0;
static PyObject* my_PyEval_EvalCode(PyObject *co, PyObject *globals, PyObject *locals)
{
    IFDEBUG printf("-> PyEval_EvalCode\n");
    PyObject*(*func)(PyObject *, PyObject*, PyObject*) = interceptor_get_original_addr("PyEval_EvalCode");

    to_str_pyobject(" |--> co: %s\n", co);
    to_str_pyobject(" |--> globals: %s\n", globals);
    to_str_pyobject(" |--> locals: %s\n", locals);

    PyObject* co_filename = real_PyObject_GetAttrString(co, "co_filename");
    char filename[1024];
    to_str_pyobject_buffer(filename, sizeof(filename)-1, co_filename);
    real_Py_DecRef(co_filename);
    IFDEBUG printf(" |--> filename: %s\n", filename);


    PyObject* code_o = real_PyCode_GetCode((PyCodeObject*)co);
    char* code = NULL;
    Py_ssize_t code_length = 0;
    real_PyBytes_AsStringAndSize(code_o, &code, &code_length);
    IFDEBUG printf(" |--> code size %li\n", code_length);

    if (my_PyEval_EvalCode_inject_next)
    {
        my_PyEval_EvalCode_inject_next = 0;
        PyObject* result = real_PyRun_String((const char *)moduleedit, Py_file_input, globals, locals);
        if (result)
        {
            real_Py_DecRef(result);
        }
        else
        {
            real_PyErr_Print();
            real_PyErr_Clear();
        }
        if (options.print_imports)
        {
            PyObject* result = real_PyRun_String("ModuleEdit.print_imports(True)", Py_file_input, globals, locals);
            if (result)
            {
                real_Py_DecRef(result);
            }
            else
            {
                real_PyErr_Print();
                real_PyErr_Clear();
                exit(1);
            }
        }
        if (options.script_name)
        {
            int fd = open(options.script_name, O_RDONLY);
            if (fd >= 0)
            {
                off_t fd_size = lseek(fd, 0L, SEEK_END);
                if (fd_size >= 0)
                {
                    size_t pagesize = getpagesize();
                    size_t mapsize = (fd_size + 1 + pagesize - 1) & (~(pagesize -1));
                    void* script_code = mmap(NULL, mapsize, PROT_READ, MAP_SHARED, fd, 0);
                    if (script_code)
                    {
                        PyObject* extract_code_o = real_Py_CompileString(script_code, options.script_name, Py_file_input);
                        if (extract_code_o)
                        {
                            PyObject* result = func(extract_code_o, globals, locals);
                            real_Py_DecRef(extract_code_o);
                            if (result)
                            {
                                real_Py_DecRef(result);
                            }
                            else
                            {
                                real_PyErr_Print();
                                real_PyErr_Clear();
                            }
                        }
                        else
                        {
                            dprintf(STDERR_FILENO, "could not compile %s\n", options.script_name);
                            real_PyErr_Print();
                            real_PyErr_Clear();
                            exit(EXIT_FAILURE);
                        }
                        munmap(script_code, mapsize);
                    }
                    else
                    {
                        dprintf(STDERR_FILENO, "%s\n", strerror(errno));
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    dprintf(STDERR_FILENO, "%s: %s\n", strerror(errno), options.script_name);
                    exit(EXIT_FAILURE);
                }
            }
            else
            {
                dprintf(STDERR_FILENO, "%s: %s\n", strerror(errno), options.script_name);
                exit(EXIT_FAILURE);
            }
        }
    }
    const char magic_script[] = "pyi_rth_inspect.py";
    size_t filename_len = strlen(filename);
    if (filename_len >= sizeof(magic_script)-1 && strcmp(magic_script, &filename[filename_len - sizeof(magic_script)+1] ) == 0)
    {
        my_PyEval_EvalCode_inject_next = 1;
    }
    return func(co, globals, locals);
}



static PyObject* my_PyImport_AddModule(const char* name)
{
    IFDEBUG printf("-> PyImport_AddModule(\"%s\")\n", name);
    PyObject*(*func)(const char *) = interceptor_get_original_addr("PyImport_AddModule");
    return func(name);
}



static PyStatus my_PyConfig_SetWideStringList(PyConfig *config, PyWideStringList *list, Py_ssize_t length, wchar_t **items)
{
    IFDEBUG printf("-> PyConfig_SetWideStringList(%p, %p, %zu, %p)\n", config, list, length, items);
    for(long int i = 0; i < length; i++)
    {
        IFDEBUG printf(" |--> %ls\n", items[i]);
    }
    PyStatus(*func)(PyConfig *, PyWideStringList*, Py_ssize_t, wchar_t**) = interceptor_get_original_addr("PyConfig_SetWideStringList");
    return func(config, list, length, items);
}



static PyObject* my_PyImport_ExecCodeModule(const char *name, PyObject *co)
{
    IFDEBUG printf("-> PyImport_ExecCodeModule(%s, %p)\n", name, co);
    PyObject*(*func)(const char*, PyObject*) = interceptor_get_original_addr("PyImport_ExecCodeModule");
    return func(name, co);
}



static int my_PyObject_SetAttrString(PyObject *o, const char *attr_name, PyObject *v)
{
    IFDEBUG printf("-> PyObject_SetAttrString(");
    to_str_pyobject("\"%s\"", o);
    IFDEBUG printf(", \"%s\"", attr_name);
    to_str_pyobject(", \"%s\")\n", v);
    int(*func)(PyObject*, const char*, PyObject*) = interceptor_get_original_addr("PyObject_SetAttrString");
    return func(o, attr_name, v);
}



static int my_PySys_SetObject(const char *name, PyObject *v)
{
    IFDEBUG printf("-> PySys_SetObject(\"%s\", ", name);
    to_str_pyobject("\%s\")\n", v);
    int(*func)(const char*, PyObject*) = interceptor_get_original_addr("PySys_SetObject");
    return func(name, v);
}



static int my_PyRun_SimpleStringFlags(const char *command, PyCompilerFlags* flags)
{
    IFDEBUG printf("-> PyRun_SimpleStringFlags(\"%s\", %p)\n", command, flags);
    int(*func)(const char *, PyCompilerFlags*) = interceptor_get_original_addr("PyRun_SimpleStringFlags");
    return func(command, flags);
}



static PyObject* my_PyModule_GetDict(PyObject* module)
{
    PyObject*(*func)(PyObject*) = interceptor_get_original_addr("PyModule_GetDict");
    PyObject* res = func(module);
    IFDEBUG printf("-> PyModule_GetDict(%p) -> %p\n", module, res);
    to_str_pyobject(" |--> %s\n", res);
    return res;
}



static PyObject* my_PyUnicode_DecodeFSDefault(const char* str)
{
    PyObject*(*func)(const char*) = interceptor_get_original_addr("PyUnicode_DecodeFSDefault");
    PyObject* res = func(str);
    IFDEBUG printf("-> PyUnicode_DecodeFSDefault(\"%s\") -> %p\n", str, res);
    return res;
}



enum {
    USED_HOOKED,
    USED_NOT_HOOKED,
    UNUSED
};



int print_dlsymd_func(const char* symbol, void* orig, void* hook, uint32_t count, void* ctx)
{
    size_t op = (size_t)ctx;
    (void)orig;
    if (op == USED_HOOKED && count && hook)
    {
        IFDEBUG printf("symbol %s, count %u\n", symbol, count);
    }
    else if (op == USED_NOT_HOOKED && count && !hook)
    {
        IFDEBUG printf("symbol %s, count %u\n", symbol, count);
    }
    else if (op == UNUSED && !count)
    {
        IFDEBUG printf("symbol %s, count %u, hooked %s\n", symbol, count, hook ? "true" : "false");
    }
    return 0;
}



void interceptor_deinit()
{
    IFDEBUG printf("USED_NOT_HOOKED\n");
    interceptor_iterate_dlsym_entries(print_dlsymd_func, (void*)USED_NOT_HOOKED);
    IFDEBUG printf("USED_HOOKED\n");
    interceptor_iterate_dlsym_entries(print_dlsymd_func, (void*)USED_HOOKED);
    IFDEBUG printf("UNUSED\n");
    interceptor_iterate_dlsym_entries(print_dlsymd_func, (void*)UNUSED);
    if (options.script_name)
    {
        free(options.script_name);
        options.script_name = NULL;
    }
}
