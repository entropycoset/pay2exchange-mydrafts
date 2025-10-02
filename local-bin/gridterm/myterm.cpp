#include "myterm.h"


#include <QShortcut>
#include <QKeySequence>


// -------------------------------------------------------

#include <iostream>
#include <QLabel>
#include <QVBoxLayout>
#include <QPalette>
#include <iostream>


void addOverlay(QWidget *parent) {
    QLabel *overlay = new QLabel("Overlay text", parent);

    // Make it transparent to mouse and focus
    overlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    overlay->setAttribute(Qt::WA_NoSystemBackground);
    overlay->setAttribute(Qt::WA_TranslucentBackground);
    overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    overlay->setFocusPolicy(Qt::NoFocus);

    // Style: semi-transparent white text, no background
    overlay->setStyleSheet("color: rgba(255, 255, 255, 128);"
                           "background: transparent;"
                           "font-size: 14px;");

    // Position it — e.g., top-right corner
    overlay->move(parent->width() - 100, 10);
    overlay->resize(90, 20);
    overlay->show();
}

// -------------------------------------------------------

void MyTerm::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.addAction("Copy", this, &QTermWidget::copyClipboard);
    menu.addAction("Paste", this, &QTermWidget::pasteClipboard);
    menu.exec(event->globalPos());

    new QShortcut(QKeySequence("F9"), this, SLOT(close()));

    addOverlay(this);
}

// POSIX-safe shell quoting: wraps in '...' and turns ' into '\''.
static QString shellQuote(const QString &s) {
    if (s.isEmpty()) return "''";
    QString r = s;
    r.replace("'", "'\\''");
    return "'" + r + "'";
}

void MyTerm::run_cmd(QString cmd, std::vector< std::string > args) {
    QString fullcmd = cmd;
    for (const auto & arg : args) {
        fullcmd = fullcmd + " " + shellQuote( QString::fromStdString(arg) );
    }
    fullcmd = fullcmd + "\r";
           std::string debug;
    debug += "Will run command [" + cmd.toStdString() + "] with args:\n";
    for (const auto &arg: args) { debug += arg + "\n"; }
    debug += "Full command: [" + fullcmd.toStdString() + "]\n";
    std::cout << debug;
    this->sendText(fullcmd);
}

//  TODO: this does not clean up the warnings - virtual KPtyProcess::~KPtyProcess() the terminal process is still running, trying to stop it by SIGHUP
void MyTerm::closeEvent(QCloseEvent *event) {
    this->deleteLater();
}
