//
// LICENSE:
//
// Copyright (c) 2016 -- 2017 Fabio Pellacini
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice,
// this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//

#include "../yocto/yocto_gl.h"
using namespace ygl;

// Application state
struct app_state {
    scene* scn = nullptr;
    camera* view = nullptr;
    string filename;
    string imfilename;
    float exposure = 0, gamma = 2.2f;
    bool filmic = false;
    vec4f background = {0, 0, 0, 0};
    trace_params params;
    bool save_progressive = false;
    trace_state* state = nullptr;

    ~app_state() {
        if (state) delete state;
        if (scn) delete scn;
        if (view) delete view;
    }
};

int main(int argc, char* argv[]) {
    // create empty scene
    auto app = new app_state();

    // parse command line
    auto parser = make_parser(argc, argv, "ytrace", "offline oath tracing");
    app->params.camera_id = 0;
    app->save_progressive =
        parse_flag(parser, "--save-progressive", "", "save progressive images");
    app->params.rtype = parse_opt(parser, "--random", "", "random type",
        trace_rng_names(), trace_rng_type::stratified);
    app->params.ftype = parse_opt(parser, "--filter", "", "filter type",
        trace_filter_names(), trace_filter_type::box);
    app->params.stype =
        parse_opt(parser, "--shader", "-S", "path estimator type",
            trace_shader_names(), trace_shader_type::pathtrace);
    app->params.envmap_invisible =
        parse_flag(parser, "--envmap-invisible", "", "envmap invisible");
    app->params.shadow_notransmission = parse_flag(
        parser, "--shadow-notransmission", "", "shadow without transmission");
    app->params.block_size =
        parse_opt(parser, "--block-size", "", "block size", 32);
    app->params.batch_size =
        parse_opt(parser, "--batch-size", "", "batch size", 16);
    app->params.nsamples =
        parse_opt(parser, "--samples", "-s", "image samples", 256);
    app->params.parallel =
        !parse_flag(parser, "--no-parallel", "", "so not run in parallel");
    app->exposure =
        parse_opt(parser, "--exposure", "-e", "hdr image exposure", 0.0f);
    app->gamma = parse_opt(parser, "--gamma", "-g", "hdr image gamma", 2.2f);
    app->filmic = parse_flag(parser, "--filmic", "-F", "hdr filmic output");
    app->params.height =
        parse_opt(parser, "--resolution", "-r", "image resolution", 540);
    auto amb = parse_opt(parser, "--ambient", "", "ambient factor", 0.0f);
    auto camera_lights =
        parse_flag(parser, "--camera-lights", "-c", "enable camera lights");
    app->params.ambient = {amb, amb, amb};
    if (camera_lights) { app->params.stype = trace_shader_type::eyelight; }
    auto log_filename = parse_opt(parser, "--log", "", "log to disk", ""s);
    if (log_filename != "") add_file_stream(log_filename, true);
    app->imfilename =
        parse_opt(parser, "--output-image", "-o", "image filename", "out.hdr"s);
    app->filename = parse_arg(parser, "scene", "scene filename", ""s);
    if (should_exit(parser)) {
        printf("%s\n", get_usage(parser).c_str());
        exit(1);
    }

    // setting up rendering
    log_info("loading scene {}", app->filename);
    try {
        app->scn = load_scene(app->filename);
    } catch (exception e) {
        log_fatal("cannot load scene {}", app->filename);
        return 1;
    }

    // add elements
    auto opts = add_elements_options();
    opts.pointline_radius = 0.001f;
    add_elements(app->scn, opts);

    // view camera
    app->view = make_view_camera(app->scn, app->params.camera_id);
    app->params.camera_id = -1;

    // build bvh
    log_info("building bvh");
    build_bvh(app->scn);

    // init renderer
    log_info("initializing tracer");
    update_lights(app->scn, true, true);

    // initialize rendering objects
    auto cam = (app->params.camera_id < 0) ?
                   app->view :
                   app->scn->cameras[app->params.camera_id];
    app->params.width = (int)round(cam->aspect * app->params.height);
    app->state = make_trace_state(app->params);

    // render
    log_info("starting renderer");
    for (auto cur_sample = 0; cur_sample < app->params.nsamples;
         cur_sample += app->params.batch_size) {
        if (app->save_progressive && cur_sample) {
            auto imfilename = format("{}{}.{}{}", path_dirname(app->imfilename),
                path_basename(app->imfilename), cur_sample,
                path_extension(app->imfilename));
            log_info("saving image {}", imfilename);
            save_image(imfilename, get_trace_image(app->state), app->exposure,
                app->gamma, app->filmic);
        }
        log_info("rendering sample {}/{}", cur_sample, app->params.nsamples);
        trace_samples(app->state, app->scn, app->view, app->scn->bvh,
                      app->params.batch_size, app->params);
    }
    log_info("rendering done");

    // save image
    log_info("saving image {}", app->imfilename);
    save_image(app->imfilename, get_trace_image(app->state), app->exposure,
        app->gamma, app->filmic);

    // cleanup
    delete app;

    // done
    return 0;
}
