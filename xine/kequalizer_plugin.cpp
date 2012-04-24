/*  This file is part of the KDE project
    Copyright (C) 2009 Artur Szymiec <artur.szymiec@gmail.com>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.

*/

/* Equalizer GPL code Copyright 2001 Anders Johansson ajh@atri.curtin.edu.au
   Equalizer filter, implementation of a 10 band time domain graphic
   equalizer using IIR filters. The IIR filters are implemented using a
   Direct Form II approach, but has been modified (b1 == 0 always) to
   save computation.
   
   Adopted to phnon xine engine plugin by Artur Szymiec in 2009 artur.szymiec@gmail.com
*/

#ifndef I18N_NOOP
#define I18N_NOOP(x) x
#endif

#include "backend.h"

#include <QObject>
#include <cmath>

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include <xine.h>
extern "C" {
// xine headers use the reserved keyword this:
#define this this_xine
#include <xine/compat.h>
#include <xine/post.h>
#include <xine/xineutils.h>
#undef this

#define KEQUALIZER_MAX_GAIN 12.0
#define KEQUALIZER_L       2      // Storage for filter taps
#define KEQUALIZER_KM      10     // Max number of bands 
#define KEQUALIZER_Q       1.2247449 
/* Q value for band-pass filters 1.2247=(3/2)^(1/2)
gives 4dB suppression @ Fc*2 and Fc/2 */
#define KEQUALIZER_CF {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000}
// Maximum and minimum gain for the bands
#define KEQUALIZER_G_MAX   +12.0
#define KEQUALIZER_G_MIN   -12.0
#define KEQUALIZER_CHANNELS_MAX 6

typedef struct
{
    post_class_t post_class;
    xine_t *xine;
} kequalizer_class_t;

typedef struct KEqualizerPlugin
{
    post_plugin_t post;

    /* private data */
    pthread_mutex_t    lock;
    xine_post_in_t params_input;

    int rate;
    int bits;
    double preAmp;
    double eqBands[10];
    //kequalizer_s kequalizer_t;
    float   a[KEQUALIZER_KM][KEQUALIZER_L];             // A weights
    float   b[KEQUALIZER_KM][KEQUALIZER_L];             // B weights
    float   wq[KEQUALIZER_CHANNELS_MAX][KEQUALIZER_KM][KEQUALIZER_L];    // Circular buffer for W data
    float   g[KEQUALIZER_CHANNELS_MAX][KEQUALIZER_KM];        // Gain factor for each channel and band
    int     K;                    // Number of used eq bands
    int     channels;             // Number of channels
    /* Functions */
    void equalize_Buffer(xine_post_t *this_gen,audio_buffer_t *buf);
    void eq_calc_Bp2(float* a, float* b, float fc, float q);
    void eq_calc_Gains(xine_post_t *this_gen);
    void eq_setup_Filters(xine_post_t *this_gen);
} kequalizer_plugin_t;

/**************************************************************************
 * parameters
 *************************************************************************/

typedef struct
{
    double preAmp;
    double eqBands[10];
} kequalizer_parameters_t;

/*
 * description of params struct
 */
START_PARAM_DESCR(kequalizer_parameters_t)

PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, preAmp, NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Equalizer pre-amp gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[0], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 1 60Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[1], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 2 170Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[2], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 3 310Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[3], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 4 600Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[4], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 5 1000Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[5], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 6 3000Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[6], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 7 6000Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[7], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 8 12000Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[8], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 9 14000Hz Gain"))
PARAM_ITEM(POST_PARAM_TYPE_DOUBLE, eqBands[9], NULL, -KEQUALIZER_MAX_GAIN, KEQUALIZER_MAX_GAIN, 0, I18N_NOOP("Band 10 16000Hz Gain"))

END_PARAM_DESCR(param_descr)

static int set_parameters (xine_post_t *this_gen, void *param_gen) 
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);
    kequalizer_parameters_t *param = static_cast<kequalizer_parameters_t *>(param_gen);

    pthread_mutex_lock (&that->lock);
    
    that->preAmp = param->preAmp;
    for (int i=0;i<=9;i++){
        that->eqBands[i]=param->eqBands[i];
    }
  
    that->eq_calc_Gains(this_gen);
    
    const char *x = "kequalizer:";
    Phonon::Xine::debug() << Q_FUNC_INFO
        << x
        << param->preAmp
        << param->eqBands[0]
        << param->eqBands[1]
        << param->eqBands[2]
        << param->eqBands[3]
        << param->eqBands[4]
        << param->eqBands[5]
        << param->eqBands[6]
        << param->eqBands[7]
        << param->eqBands[8]
        << param->eqBands[9]
        ;    
    pthread_mutex_unlock (&that->lock);
    
    return 1;
}

static int get_parameters (xine_post_t *this_gen, void *param_gen) 
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);
    kequalizer_parameters_t *param = static_cast<kequalizer_parameters_t *>(param_gen);

    pthread_mutex_lock (&that->lock);
    
    param->preAmp = that->preAmp;
    for (int i=0;i<=9;i++){
        param->eqBands[i]=that->eqBands[i];
    }
    
    pthread_mutex_unlock (&that->lock);

    return 1;
}

static xine_post_api_descr_t *get_param_descr()
{
    return &param_descr;
}

static char *get_help ()
{
    static QByteArray helpText(
           QObject::tr("Equalizes audio using the very good IIR equalizer code by  "
                 "Anders Johansson adopted from Audacious project.\n"
                 "\n"
                 "Parameters:\n"
                 "Preamp gain - used to alter up or down all gain values\n"
                 "10 Equalizer bands - actual IIR equalizer parameters.\n").toUtf8());
    return helpText.data();
}

static xine_post_api_t post_api = {
    set_parameters,
    get_parameters,
    get_param_descr,
    get_help,
};


/**************************************************************************
 * xine audio post plugin functions
 *************************************************************************/

static int kequalizer_port_open(xine_audio_port_t *port_gen, xine_stream_t *stream,
                             uint32_t bits, uint32_t rate, int mode)
{
    post_audio_port_t *port = reinterpret_cast<post_audio_port_t *>(port_gen);
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(port->post);
    xine_post_t *post = reinterpret_cast<xine_post_t *>(port->post);

    _x_post_rewire(&that->post);
    _x_post_inc_usage(port);

    port->stream = stream;
    port->bits = bits;
    port->rate = rate;
    port->mode = mode;
    that->rate = rate;
    that->bits = bits;
    
    switch (mode) {
    case AO_CAP_MODE_STEREO:
        that->channels = 2;
        break;
    case AO_CAP_MODE_4CHANNEL:
        that->channels = 4;
        break;
    case AO_CAP_MODE_4_1CHANNEL:
    case AO_CAP_MODE_5CHANNEL:
    case AO_CAP_MODE_5_1CHANNEL:
        that->channels = 6;
        break;
    }
    
    that->eq_setup_Filters(post);
    that->eq_calc_Gains(post);
    
    return port->original_port->open(port->original_port, stream, bits, rate, mode);
}

static void kequalizer_port_close(xine_audio_port_t *port_gen, xine_stream_t *stream)
{
    post_audio_port_t *port = reinterpret_cast<post_audio_port_t *>(port_gen);

    port->stream = NULL;
    port->original_port->close(port->original_port, stream);
    _x_post_dec_usage(port);
}

static void kequalizer_port_put_buffer(xine_audio_port_t *port_gen,
        audio_buffer_t *buf, xine_stream_t *stream)
{
    post_audio_port_t *port = reinterpret_cast<post_audio_port_t *>(port_gen);
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(port->post);
    xine_post_t *post = reinterpret_cast<xine_post_t *>(port->post);
    
    // Do actual equalization
    that->equalize_Buffer(post,buf);
    // and send the modified buffer to the original port
    port->original_port->put_buffer(port->original_port, buf, stream);
    return;
}

static void kequalizer_dispose(post_plugin_t *this_gen)
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);

    if (_x_post_dispose(this_gen)) {
        pthread_mutex_destroy(&that->lock);
        free(that);
    }
}

/* plugin class functions */
static post_plugin_t *kequalizer_open_plugin(post_class_t *class_gen, int inputs,
                                          xine_audio_port_t **audio_target,
                                          xine_video_port_t **video_target)
{
    Q_UNUSED(class_gen);
    Q_UNUSED(inputs);
    Q_UNUSED(video_target);
    kequalizer_plugin_t *that;
    posix_memalign((void**)(&that), 2, sizeof(kequalizer_plugin_t));
    post_in_t           *input;
    post_out_t          *output;
    xine_post_in_t      *input_api;
    post_audio_port_t   *port;

    // refuse to work without an audio port to decorate
    if (!that || !audio_target || !audio_target[0]) {
        free(that);
        return NULL;
    }

    // creates 1 audio I/O, 0 video I/O
    _x_post_init(&that->post, 1, 0);
    pthread_mutex_init (&that->lock, NULL);

    // init private data
   
    // the following call wires our plugin in front of the given audio_target
    port = _x_post_intercept_audio_port(&that->post, audio_target[0], &input, &output);
    // the methods of new_port are all forwarded to audio_target, overwrite a few of them here:
    port->new_port.open       = kequalizer_port_open;
    port->new_port.close      = kequalizer_port_close;
    port->new_port.put_buffer = kequalizer_port_put_buffer;

    // add a parameter input to the plugin
    input_api       = &that->params_input;
    input_api->name = "parameters";
    input_api->type = XINE_POST_DATA_PARAMETERS;
    input_api->data = &post_api;
    xine_list_push_back(that->post.input, input_api);

    that->post.xine_post.audio_input[0] = &port->new_port;

    // our own cleanup function
    that->post.dispose = kequalizer_dispose;

    return &that->post;
}

#if XINE_MAJOR_VERSION < 1 || (XINE_MAJOR_VERSION == 1 && (XINE_MINOR_VERSION < 1 || (XINE_MINOR_VERSION == 1 && XINE_SUB_VERSION < 90)))
#define NEED_DESCRIPTION_FUNCTION 1
#else
#define NEED_DESCRIPTION_FUNCTION 0
#endif

#define PLUGIN_DESCRIPTION I18N_NOOP("Fade in or fade out with different fade curves")
#define PLUGIN_IDENTIFIER "KVolumeFader"

#if NEED_DESCRIPTION_FUNCTION
static char *kequalizer_get_identifier(post_class_t *class_gen)
{
    Q_UNUSED(class_gen);
    return PLUGIN_IDENTIFIER;
}

static char *kequalizer_get_description(post_class_t *class_gen)
{
    Q_UNUSED(class_gen);
    static QByteArray description(QObject::tr(PLUGIN_DESCRIPTION).toUtf8());
    return description.data();
}
#endif

static void kequalizer_class_dispose(post_class_t *class_gen)
{
    free(class_gen);
}

/* plugin class initialization function */
void *init_kequalizer_plugin (xine_t *xine, void *)
{
    kequalizer_class_t *_class = static_cast<kequalizer_class_t *>(malloc(sizeof(kequalizer_class_t)));

    if (!_class) {
        return NULL;
    }

    _class->post_class.open_plugin     = kequalizer_open_plugin;
#if NEED_DESCRIPTION_FUNCTION
    _class->post_class.get_identifier  = kequalizer_get_identifier;
    _class->post_class.get_description = kequalizer_get_description;
#else
    _class->post_class.description     = PLUGIN_DESCRIPTION;
    _class->post_class.text_domain     = "phonon-xine";
    _class->post_class.identifier      = PLUGIN_IDENTIFIER;
#endif
    _class->post_class.dispose         = kequalizer_class_dispose;

    _class->xine                       = xine;

    return _class;
}

/* Filter functions */

// 2nd order Band-pass Filter design
void KEqualizerPlugin::eq_calc_Bp2(float* a, float* b, float fc, float q)
{ 
    double th= 2.0 * M_PI * fc;
    double C = (1.0 - tan(th*q/2.0))/(1.0 + tan(th*q/2.0));

    a[0] = (1.0 + C) * cos(th);
    a[1] = -1 * C;
  
    b[0] = (1.0 - C)/2.0;
    b[1] = -1.0050;
}

void KEqualizerPlugin::eq_calc_Gains(xine_post_t *this_gen)
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);
    // Sanity check
    if(that->channels<1 || that->channels>KEQUALIZER_CHANNELS_MAX)
       return;
    // adjust gains including preamp value
    float b[10];
    float adj = 0.0;
    
    // Get bands from config
    for(int i = 0; i < 10; i++){
        b[i] = that->eqBands[i] + that->preAmp;
    }   
    
    for(int i = 0; i < 10; i++)
        if(fabsf(b[i]) > fabsf(adj)) adj = b[i];

    if(fabsf(adj) > KEQUALIZER_G_MAX) {
        adj = adj > 0.0 ? KEQUALIZER_G_MAX - adj : -KEQUALIZER_G_MAX - adj;
        for(int i = 0; i < 10; i++) b[i] += adj;
    }
     // Recalculate set gains to internal coeficient gains
    for(int i=0;i<that->channels;i++){
        
        for(int k = 0 ; k<KEQUALIZER_KM ; k++){
            if(b[k] > KEQUALIZER_G_MAX){
                b[k]=KEQUALIZER_G_MAX;
            }else if(b[k] < KEQUALIZER_G_MIN){
                b[k]=KEQUALIZER_G_MIN;
            }
            that->g[i][k] = pow(10.0,b[k]/20.0)-1.0;
       }
    }
}

void KEqualizerPlugin::eq_setup_Filters(xine_post_t *this_gen)
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);
    int k =0;
    float F[KEQUALIZER_KM] = KEQUALIZER_CF;

    // Calculate number of active filters
    that->K=KEQUALIZER_KM;
    while(F[that->K-1] > (float)that->rate/(KEQUALIZER_Q*2.0))
      that->K--;
    
    if(that->K != KEQUALIZER_KM){
        Phonon::Xine::debug() << Q_FUNC_INFO 
        << "[kequalizer] Limiting the number of filters to" 
        << "due to low sample rate =>"
        << that->K;
    }
    // Generate filter taps
    for(k=0;k<that->K;k++)
      that->eq_calc_Bp2(that->a[k],that->b[k],F[k]/((float)that->rate),KEQUALIZER_Q);
}

void KEqualizerPlugin::equalize_Buffer(xine_post_t *this_gen, audio_buffer_t *buf)
{
    kequalizer_plugin_t *that = reinterpret_cast<kequalizer_plugin_t *>(this_gen);
    const int bufferLength = buf->num_frames * that->channels;
    
    if (buf->format.bits == 16 || buf->format.bits == 0) {
        int16_t         ci   = that->channels;            // Index for channels
        int16_t         nch  = that->channels;             // Number of channels

        while(ci--){
        float*        g   = that->g[ci];      // Gain factor 
        int16_t*      in  = ((int16_t*)static_cast<int16_t *>(buf->mem))+ci;
        int16_t*      out = ((int16_t*)static_cast<int16_t *>(buf->mem))+ci;
        int16_t*      end = in + bufferLength;//sizeof(int16_t); // Block loop end
        
            while(in < end){
                  register int      k  = 0;         // Frequency band index
                  register float    yt = *in;       // Current input sample
                  in+=nch;
         
                  // Run the filters
                  for(;k<that->K;k++){
                        // Pointer to circular buffer wq
                        register float* wq = that->wq[ci][k];
                        // Calculate output from AR part of current filter
                        register float w=yt*that->b[k][0] + wq[0]*that->a[k][0] + wq[1]*that->a[k][1];
                        // Calculate output form MA part of current filter
                        yt+=(w + wq[1]*that->b[k][1])*g[k];
                        // Update circular buffer
                        wq[1] = wq[0];
                        wq[0] = w;
                  }
                  // Output data to buffer 
                  // NOTE maybe we need to add more sophisticated convertion method from float to ine like in libSAD with dithering ??
                  // NOTE for now this clipping have to be enough
                  *out =  yt <= (float)32767 ? ( yt >= (float)-32768 ? (int16_t)yt : -32768 ) : 32767;
                  out+=nch;//nch;
                  } 
        }
    }else{
        Phonon::Xine::debug() << Q_FUNC_INFO << "broken bits " << buf->format.bits;    
    }
}


} // extern "C"
