#include "myterm.h"


#include <QShortcut>
#include <QKeySequence>


void MyTerm::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.addAction("Copy", this, &QTermWidget::copyClipboard);
    menu.addAction("Paste", this, &QTermWidget::pasteClipboard);
    menu.exec(event->globalPos());

    new QShortcut(QKeySequence("F9"), this, SLOT(close()));
}

// POSIX-safe shell quoting: wraps in '...' and turns ' into '\''.
static QString shellQuote(const QString &s) {
    if (s.isEmpty()) return "''";
    QString r = s;
    r.replace("'", "'\\''");
    return "'" + r + "'";
}

void MyTerm::run_cmd(QString cmd, std::vector< QString > args) {
    QString fullcmd = cmd;
    for (const auto & arg : args) {
        fullcmd = fullcmd + " " + shellQuote(arg);
    }
    fullcmd = fullcmd + "\r";
    this->sendText(fullcmd);
}

//  TODO: this does not clean up the warnings - virtual KPtyProcess::~KPtyProcess() the terminal process is still running, trying to stop it by SIGHUP
void MyTerm::closeEvent(QCloseEvent *event) {
    this->deleteLater();
}
