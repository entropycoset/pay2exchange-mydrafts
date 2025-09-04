#pragma once
#include <qtermwidget.h>
#include <QMenu>
#include <QContextMenuEvent>
#include <vector>

class MyTerm : public QTermWidget {
    Q_OBJECT

public:
    using QTermWidget::QTermWidget; // inherit constructors
    void closeEvent(QCloseEvent *event);

    void run_cmd(QString cmd, std::vector< std::string > args);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
};
