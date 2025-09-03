#pragma once
#include <qtermwidget.h>
#include <QMenu>
#include <QContextMenuEvent>

class MyTerm : public QTermWidget {
    Q_OBJECT
public:
    using QTermWidget::QTermWidget; // inherit constructors

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
};
