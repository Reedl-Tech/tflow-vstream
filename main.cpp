#include <thread>
#include <csignal>
#include <sys/stat.h>

#include <giomm.h>
#include <glib-unix.h>

#include "tflow-vstream.h"

TFlowVStream *gp_app;

gboolean handle_signal_pipe(gpointer ctx)
{
    g_info("Got PIPE signal");

    TFlowVStream* app = (TFlowVStream*)ctx;

    return true;
}

gboolean handle_signal(gpointer ctx)
{
    g_info("Got INT or TERM signal, terminating...");

    TFlowVStream *app = (TFlowVStream*)ctx;
    g_main_loop_quit(app->main_loop);

    return true;
}

gboolean handle_signal_hup(gpointer ctx)
{
    g_info("Got HUP signal, reloading configuration... under construction");

    //TFlowVStream* app = (TFlowVStream*)ctx;
    //g_main_loop_quit(app->main_loop);

    return true;
}


static void setup_sig_handlers()
{
    GSource *src_sigint, *src_sigterm, *src_sighup;

    src_sigint = g_unix_signal_source_new(SIGINT);
    src_sigterm = g_unix_signal_source_new(SIGTERM);
    src_sighup = g_unix_signal_source_new(SIGHUP);

    g_source_set_callback(src_sigint,  (GSourceFunc)handle_signal,     gp_app, NULL);
    g_source_set_callback(src_sigterm, (GSourceFunc)handle_signal,     gp_app, NULL);
    g_source_set_callback(src_sighup,  (GSourceFunc)handle_signal_hup, gp_app, NULL);

    g_source_attach(src_sigint, gp_app->context);
    g_source_attach(src_sigterm, gp_app->context);
    g_source_attach(src_sighup, gp_app->context);

    g_source_unref(src_sigint);
    g_source_unref(src_sigterm);
    g_source_unref(src_sighup);
}

void getConfigFilename(int argc, char* argv, std::string cfg_fname)
{
    if (argc > 1) {
        std::string config_fname_in = argv;
        struct stat sb;
        int cfg_fd = open(config_fname_in.c_str(), O_RDWR);

        if (cfg_fd == -1 || fstat(cfg_fd, &sb) < 0 || !S_ISREG(sb.st_mode)) {
            g_warning("Can't open configuration file %s. Will try to use default %s",
                config_fname_in.c_str(), cfg_fname.c_str());
            return;
        }
    }
}

int main(int argc, char** argv)
{
    Gio::init();

    g_info("TFlow Video Streamer started");

    std::string cfg_fname("/etc/tflow/tflow-vstream-config.json");
    getConfigFilename(argc, argv[1], cfg_fname);

    gp_app = new TFlowVStream(cfg_fname);

    setup_sig_handlers();

    signal(SIGPIPE, SIG_IGN);   // Block SIGPIPE signal

    gp_app->AttachIdle();

#if CODE_BROWSE
    gp_app->OnIdle();   
#endif

    g_main_loop_run(gp_app->main_loop);

    delete gp_app;

    g_info("TFlow Video Streamer thread exited");

    return 0;
}
