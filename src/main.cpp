#include "aboutdialog.h"
#include "config.h"
#include "headless.h"
#include "mainwindow.h"
#include "mapview.h"

#include "cubiomes/util.h"

#include <QApplication>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStandardPaths>

#include <cstdlib>
#include <cstring>
#include <string>

extern "C"
int getStructureConfig_override(int stype, int mc, StructureConfig *sconf)
{
    if unlikely(mc == INT_MAX) // to check if override is enabled in cubiomes
        mc = 0;
    int ok = getStructureConfig(stype, mc, sconf);
    if (ok && g_extgen.saltOverride)
    {
        uint64_t salt = g_extgen.salts[stype];
        if (salt <= MASK48)
            sconf->salt = salt;
    }
    return ok;
}

int main(int argc, char *argv[])
{
    initBiomeColors(g_biomeColors);
    initBiomeTypeColors(g_tempsColors);

    QCoreApplication::setApplicationName(APP_STRING);

    bool version = false;
    bool nogui = false;
    bool clear = false;
    bool reset = false;
    bool usage = false;
    uint64_t seed = 0;
    int exportWidth = 1024;
    int exportHeight = 1024;
    float zoom = 1.0;
    QString exportpath;
    QString sessionpath;
    QString resultspath;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--version") == 0)
            version = true;
        else if (strcmp(argv[i], "--nogui") == 0)
            nogui = true;
        else if (strcmp(argv[i], "--reset") == 0)
            clear = true;
        else if (strcmp(argv[i], "--reset-all") == 0)
            reset = true;
        else if (strncmp(argv[i], "--session=", 10) == 0)
            sessionpath = argv[i] + 10;
        else if (strcmp(argv[i], "--session") == 0 && i+1 < argc)
            sessionpath = argv[++i];
        else if (strncmp(argv[i], "--out=", 6) == 0)
            resultspath = argv[i] + 6;
        else if (strcmp(argv[i], "--out") == 0 && i+1 < argc)
            resultspath = argv[++i];
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
            usage = true;
        else if (strncmp(argv[i], "--export=", 9) == 0)
            exportpath = argv[i] + 9;
        else if (strcmp(argv[i], "--export") == 0 && i+1 < argc)
            exportpath = argv[++i];
        else if (strncmp(argv[i], "--seed=", 7) == 0)
            seed = (uint64_t)strtoll(argv[i] + 7, nullptr, 10);
        else if (strcmp(argv[i], "--seed") == 0 && i+1 < argc)
            seed = (uint64_t)strtoll(argv[++i], nullptr, 10);
        else if (strncmp(argv[i], "--width=", 8) == 0)
            exportWidth = atoi(argv[i] + 8);
        else if (strcmp(argv[i], "--width") == 0 && i+1 < argc)
            exportWidth = atoi(argv[++i]);
        else if (strncmp(argv[i], "--height=", 9) == 0)
            exportHeight = atoi(argv[i] + 9);
        else if (strcmp(argv[i], "--height") == 0 && i+1 < argc)
            exportHeight = atoi(argv[++i]);
        else if (strcmp(argv[i], "--zoom") == 0 && i+1 < argc)
            zoom = std::__cxx11::stof(argv[++i]);
    }

    if (usage)
    {
        const char *msg =
                "Usage: cubiomes-viewer [options]\n"
                "Options:\n"
                "      --help                 Display this help and exit.\n"
                "      --version              Output version information and exit.\n"
                "      --nogui                 Run in headless search mode.\n"
                "      --seed=N               Set world seed (use with --export).\n"
                "      --export=file          Render map to image and exit (requires --nogui).\n"
                "      --width=N              Image width for export (default 1024).\n"
                "      --height=N             Image height for export (default 1024).\n"
                "      --reset                Discard results and reset starting seed.\n"
                "      --reset-all            Clear settings and remove all session data.\n"
                "      --session=file         Open this session file.\n"
                "      --out=file             Write matching seeds to this file while searching.\n"
                "\n";
        printf("%s", msg);
        exit(0);
    }
    if (version)
    {
        printf("%s %s\n", APP_STRING, getVersStr().toLocal8Bit().data());
        exit(0);
    }

    if (reset)
    {
        QSettings settings(APP_STRING, APP_STRING);
        settings.clear();

        QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir dir(path);
        if (dir.exists() && path.contains(APP_STRING))
        {
            dir.removeRecursively();
        }
    }

    if (sessionpath.isEmpty())
    {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir dir(path);
        if (!dir.exists())
            dir.mkpath(".");
        sessionpath = path + "/session.save";
    }

    if (nogui)
    {
        if (!exportpath.isEmpty())
        {
            QApplication app(argc, argv);
            QSettings settings(APP_STRING, APP_STRING);
            g_extgen.load(settings);

            WorldInfo wi;
            wi.reset();
            wi.seed = seed;
            LayerOpt lopt;
            lopt.reset();

            MapView view(nullptr);
            view.setSeed(wi, DIM_OVERWORLD, lopt);
            view.setView(0, 0, 16);  // center 0,0 and scale 64 blocks per unit

            int w = (exportWidth > 0) ? exportWidth : 1024;
            int h = (exportHeight > 0) ? exportHeight : 1024;
            QSize size(w, h);

            // First draw triggers world->draw(), which requests quads and starts workers.
            // Workers run asynchronously; wait for them to finish before capturing.
            (void) view.renderToImage(size, false);
            view.zoom(qreal(zoom));
            if (view.world)
                view.world->waitForIdle();
            QImage img = view.renderToImage(size, true);
            if (!img.save(exportpath))
                return 1;
            return 0;
        }
        QCoreApplication app(argc, argv);
        Headless headless(sessionpath, resultspath, clear, &app);

        QObject::connect(&headless, SIGNAL(finished()), &app, SLOT(quit()));
        QTimer::singleShot(0, &headless, SLOT(run()));

        return app.exec();
    }
    else
    {
        QGuiApplication::setDesktopFileName("com.github.cubitect.cubiomes-viewer");
        QApplication::setAttribute(Qt::AA_UseStyleSheetPropagationInWidgetStyles, false);

        QApplication app(argc, argv);

        MainWindow mw(sessionpath, resultspath);
        mw.show();
        return app.exec();
    }
}
