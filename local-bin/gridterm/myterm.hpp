#pragma once
//#include <qtermwidget.h>
//#include <qtermwidget.h
#include <qtermwidget6/qtermwidget.h>
// /usr/include/qtermwidget6/qtermwidget.h
#include <QMenu>
#include <QContextMenuEvent>
#include <vector>

class MyTerm : public QTermWidget {
    Q_OBJECT

public:
    using QTermWidget::QTermWidget; // inherit constructors
    void closeEvent(QCloseEvent *event) override;

    void run_cmd(QString cmd, std::vector< std::string > args);
    void run_cmd(const std::string &cmd, std::vector< std::string > args);

public slots:
    void copyAllText();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
};
