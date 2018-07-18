/*
// Copyright (c) 2018 - GRAME CNCM - CICM - ANR MUSICOLL - Pierre Guillot.
// For information on usage and redistribution, and for a DISCLAIMER OF ALL
// WARRANTIES, see the file, "LICENSE.txt," in this distribution.
*/

#include <m_pd.h>
#include <string.h>
#include <float.h>
#include <math.h>
#include <sys/stat.h>

#include <faust/dsp/llvm-c-dsp.h>
#include "faust_tilde_ui.h"
#include "faust_tilde_io.h"
#include "faust_tilde_options.h"

#define MAXFAUSTSTRING 4096
#define MAXFAUSTOPTIONS 128

typedef struct _faustgen_tilde
{
    t_object            f_obj;
    llvm_dsp_factory*   f_dsp_factory;
    llvm_dsp*           f_dsp_instance;
    t_faust_ui_manager* f_ui_manager;
    t_faust_io_manager* f_io_manager;
    t_faust_opt_manager* f_opt_manager;
 
    t_symbol*           f_dsp_name;
    t_float             f_dummy;
    t_clock*            f_clock;
    double              f_clock_time;
    long                f_time;
} t_faustgen_tilde;

static t_class *faustgen_tilde_class;


//////////////////////////////////////////////////////////////////////////////////////////////////
//                                          FAUST INTERFACE                                     //
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faustgen_tilde_delete_instance(t_faustgen_tilde *x)
{
    if(x->f_dsp_instance)
    {
        deleteCDSPInstance(x->f_dsp_instance);
    }
    x->f_dsp_instance = NULL;
}

static void faustgen_tilde_delete_factory(t_faustgen_tilde *x)
{
    faustgen_tilde_delete_instance(x);
    if(x->f_dsp_factory)
    {
        deleteCDSPFactory(x->f_dsp_factory);
    }
    x->f_dsp_factory = NULL;
}

static void faustgen_tilde_compile(t_faustgen_tilde *x)
{
    char const* filepath;
    int dspstate = canvas_suspend_dsp();
    if(!x->f_dsp_name)
    {
        return;
    }
    filepath = faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name);
    if(filepath)
    {
        char errors[MAXFAUSTSTRING];
        int noptions            = (int)faust_opt_manager_get_noptions(x->f_opt_manager);
        char const** options    = faust_opt_manager_get_options(x->f_opt_manager);
        faustgen_tilde_delete_instance(x);
        faustgen_tilde_delete_factory(x);
        faust_ui_manager_clear(x->f_ui_manager);
        
        x->f_dsp_factory = createCDSPFactoryFromFile(filepath, noptions, options, "", errors, -1);
        if(strnlen(errors, MAXFAUSTSTRING))
        {
            pd_error(x, "faustgen~: try to load %s", filepath);
            pd_error(x, "faustgen~: %s", errors);
            x->f_dsp_factory = NULL;
            
            canvas_resume_dsp(dspstate);
            return;
        }

        x->f_dsp_instance = createCDSPInstance(x->f_dsp_factory);
        if(x->f_dsp_instance)
        {
            const int ninputs = getNumInputsCDSPInstance(x->f_dsp_instance);
            const int noutputs = getNumOutputsCDSPInstance(x->f_dsp_instance);
            logpost(x, 3, "\nfaustgen~: compilation from source '%s' succeeded", x->f_dsp_name->s_name);
            faust_io_manager_init(x->f_io_manager, ninputs, noutputs, faust_ui_manager_has_passive_ui(x->f_ui_manager));
            faust_ui_manager_init(x->f_ui_manager, x->f_dsp_instance);
            
            canvas_resume_dsp(dspstate);
            return;
        }
        
        pd_error(x, "faustgen~: memory allocation failed - instance");
        canvas_resume_dsp(dspstate);
        return;
    }
    pd_error(x, "faustgen~: source file not found %s", x->f_dsp_name->s_name);
    canvas_resume_dsp(dspstate);
}

static void faustgen_tilde_compile_options(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    faust_opt_manager_parse_compile_options(x->f_opt_manager, argc, argv);
    faustgen_tilde_compile(x);
}

static void faustgen_tilde_read(t_faustgen_tilde *x, t_symbol* s)
{
    x->f_dsp_name = s;
    faustgen_tilde_compile(x);
}

static long faustgen_tilde_get_time(t_faustgen_tilde *x)
{
    struct stat attrib;
    stat(faust_opt_manager_get_full_path(x->f_opt_manager, x->f_dsp_name->s_name), &attrib);
    return attrib.st_ctime;
}

static void faustgen_tilde_autocompile_tick(t_faustgen_tilde *x)
{
    long ntime = faustgen_tilde_get_time(x);
    if(ntime != x->f_time)
    {
        x->f_time = ntime;
        faustgen_tilde_compile(x);
    }
    clock_delay(x->f_clock, x->f_clock_time);
}

static void faustgen_tilde_autocompile(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    float state = atom_getfloatarg(0, argc, argv);
    if(fabsf(state) > FLT_EPSILON)
    {
        float time = atom_getfloatarg(1, argc, argv);
        x->f_clock_time = (time > FLT_EPSILON) ? (double)time : 100.;
        x->f_time = faustgen_tilde_get_time(x);
        clock_delay(x->f_clock, x->f_clock_time);
    }
    else
    {
        clock_unset(x->f_clock);
    }
}

static void faustgen_tilde_print_parameters(t_faustgen_tilde *x)
{
    faust_io_manager_print(x->f_io_manager, 0);
    faust_opt_manager_print(x->f_opt_manager, 0);
    faust_ui_manager_print(x->f_ui_manager, 0);
}


//////////////////////////////////////////////////////////////////////////////////////////////////
//                                  PURE DATA GENERIC INTERFACE                                 //
//////////////////////////////////////////////////////////////////////////////////////////////////

static void faustgen_tilde_anything(t_faustgen_tilde *x, t_symbol* s, int argc, t_atom* argv)
{
    if(x->f_dsp_instance)
    {
        t_float value;
        if(!faust_ui_manager_set(x->f_ui_manager, s, atom_getfloatarg(0, argc, argv)))
        {
            return;
        }
        if(!faust_ui_manager_get(x->f_ui_manager, s, &value))
        {
            t_atom av;
            SETFLOAT(&av, value);
            outlet_anything(faust_io_manager_get_extra_output(x->f_io_manager), s, 1, &av);
            return;
        }
        pd_error(x, "faustgen~: ui glue '%s' not defined", s->s_name);
        return;
    }
    pd_error(x, "faustgen~: no dsp instance");
}

static t_int *faustgen_tilde_perform(t_int *w)
{
    computeCDSPInstance((llvm_dsp *)w[1], (int)w[2], (FAUSTFLOAT **)w[3], (FAUSTFLOAT **)w[4]);
    return (w+5);
}

static void faustgen_tilde_dsp(t_faustgen_tilde *x, t_signal **sp)
{
    if(x->f_dsp_instance && faust_io_manager_is_valid(x->f_io_manager))
    {
        faust_ui_manager_save_states(x->f_ui_manager);
        initCDSPInstance(x->f_dsp_instance, sp[0]->s_sr);
        faust_io_manager_prepare(x->f_io_manager, sp);
        dsp_add((t_perfroutine)faustgen_tilde_perform, 4,
                (t_int)x->f_dsp_instance, (t_int)sp[0]->s_n,
                (t_int)faust_io_manager_get_input_signals(x->f_io_manager),
                (t_int)faust_io_manager_get_output_signals(x->f_io_manager));
        faust_ui_manager_restore_states(x->f_ui_manager);
    }
}

static void faustgen_tilde_free(t_faustgen_tilde *x)
{
    faustgen_tilde_delete_instance(x);
    faustgen_tilde_delete_factory(x);
    faust_ui_manager_free(x->f_ui_manager);
    faust_io_manager_free(x->f_io_manager);
    faust_opt_manager_free(x->f_opt_manager);
}

static void *faustgen_tilde_new(t_symbol* s, int argc, t_atom* argv)
{
    t_faustgen_tilde* x = (t_faustgen_tilde *)pd_new(faustgen_tilde_class);
    if(x)
    {
        x->f_dsp_factory    = NULL;
        x->f_dsp_instance   = NULL;
        
        x->f_ui_manager     = faust_ui_manager_new((t_object *)x);
        x->f_io_manager     = faust_io_manager_new((t_object *)x, canvas_getcurrent());
        x->f_opt_manager    = faust_opt_manager_new((t_object *)x, canvas_getcurrent());
        x->f_dsp_name       = atom_getsymbolarg(0, argc, argv);
        x->f_clock          = clock_new(x, (t_method)faustgen_tilde_autocompile_tick);
        faust_opt_manager_parse_compile_options(x->f_opt_manager, argc-1, argv+1);
        if(!argc)
        {
            return x;
        }
        faustgen_tilde_compile(x);
        if(!x->f_dsp_instance)
        {
            faustgen_tilde_free(x);
            return NULL;
        }
    }
    return x;
}

void faustgen_tilde_setup(void)
{
    t_class* c = class_new(gensym("faustgen~"),
                           (t_newmethod)faustgen_tilde_new, (t_method)faustgen_tilde_free,
                           sizeof(t_faustgen_tilde), CLASS_DEFAULT, A_GIMME, 0);
    
    if(c)
    {
        class_addmethod(c,      (t_method)faustgen_tilde_dsp,              gensym("dsp"),            A_CANT);
        class_addmethod(c,      (t_method)faustgen_tilde_compile,          gensym("compile"),        A_NULL);
        //class_addmethod(c,      (t_method)faustgen_tilde_read,             gensym("read"),           A_SYMBOL);
        class_addmethod(c,      (t_method)faustgen_tilde_compile_options,  gensym("compileoptions"), A_GIMME);
        class_addmethod(c,      (t_method)faustgen_tilde_autocompile,      gensym("autocompile"),    A_GIMME);
        class_addmethod(c,      (t_method)faustgen_tilde_print_parameters, gensym("print"),          A_NULL);
        class_addanything(c,    (t_method)faustgen_tilde_anything);
        
        CLASS_MAINSIGNALIN(c, t_faustgen_tilde, f_dummy);
        logpost(NULL, 3, "Faust website: faust.grame.fr");
        logpost(NULL, 3, "Faust development: GRAME");
        
        logpost(NULL, 3, "faustgen~ compiler version: %s", getCLibFaustVersion());
        logpost(NULL, 3, "faustgen~ default include directory: %s", class_gethelpdir(c));
        logpost(NULL, 3, "faustgen~ institutions: CICM - ANR MUSICOLL");
        logpost(NULL, 3, "faustgen~ external author: Pierre Guillot");
        logpost(NULL, 3, "faustgen~ website: github.com/CICM/faust-pd");
    }
    
    faustgen_tilde_class = c;
}

