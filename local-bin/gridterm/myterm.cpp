#include "myterm.h"

#include <qtermwidget.h>
#include <QMenu>
#include <QContextMenuEvent>

void MyTerm::contextMenuEvent(QContextMenuEvent *event) override {
    QMenu menu(this);
    menu.addAction("Copy", this, &QTermWidget::copyClipboard);
    menu.addAction("Paste", this, &QTermWidget::pasteClipboard);
    menu.exec(event->globalPos());
}
