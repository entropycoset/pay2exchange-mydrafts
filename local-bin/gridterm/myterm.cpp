#include "myterm.h"

void MyTerm::contextMenuEvent(QContextMenuEvent *event) {
    QMenu menu(this);
    menu.addAction("Copy", this, &QTermWidget::copyClipboard);
    menu.addAction("Paste", this, &QTermWidget::pasteClipboard);
    menu.exec(event->globalPos());
}
