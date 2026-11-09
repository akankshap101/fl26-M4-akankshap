#include "aiws/gui/main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QMessageBox>
#include <QStringList>
#include "aiws/gui/ipc_server.hpp"

void confirm_exit(QWidget* parent){
    QMessageBox::StandardButton answer = QMessageBox::question(
        parent, "Exit", "Are you sure?", QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if(answer == QMessageBox::Yes){
        parent->close();
    }
}
int main(int argc, char* argv[]){
    QApplication app(argc, argv);

    aiws::gui::MainWindow window;
    QCommandLineParser parser;
    parser.setApplicationDescription("Responsive AI Workspace");
    parser.addHelpOption();
    parser.addOption({"ipc-socket", "Remote Add socket path.", "PATH",
                      QString::fromStdString(aiws::gui::default_ipc_socket_path())});
    parser.addOption({"build-delay-ms", "Extra build time per document (milliseconds).", "N", "0"});
    parser.process(app);
    bool valid_delay = false;
    const int delay = parser.value("build-delay-ms").toInt(&valid_delay);
    if(!valid_delay || delay < 0){
        parser.showHelp(2);
    }
    window.set_build_delay_ms(delay);
    const auto socket_path = parser.value("ipc-socket");
    window.setWindowTitle("AI workspace");
    window.resize(900, 900);
    window.set_exit_callback(&confirm_exit);
    window.show();
    window.start_ipc_server(socket_path);

    return app.exec();
}
