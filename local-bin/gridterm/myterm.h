#ifndef MYTERM_H
#define MYTERM_H

#include <QObject>
#include <QWidget>
#include <qtermwidget.h>
#include <QMenu>
#include <QContextMenuEvent>

class MyTerm : public QTermWidget {
    Q_OBJECT
public:
    using QTermWidget::QTermWidget;
protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
};



#endif // MYTERM_H
