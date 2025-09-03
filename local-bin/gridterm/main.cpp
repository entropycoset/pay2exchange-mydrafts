#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QVector>
#include <qtermwidget.h>
#include <QMenu>
#include <QContextMenuEvent>

#include "myterm.h"

void configure_term(QTermWidget *term, int fontsize_add) {
    //term->setColor(QTermWidget::BackgroundRole, QColor(0, 0, 0));       // black background
    //term->setColor(QTermWidget::ForegroundRole, QColor(200, 200, 200)); // light gray text
    term->setColorScheme("gridterm");
    term->setColorScheme("Tango");

    QFont f = term->getTerminalFont();
    f.setPointSize(f.pointSize() + fontsize_add);
    term->setTerminalFont(f);

    term->setScrollBarPosition(QTermWidget::ScrollBarRight);
    term->setHistorySize(10000);

}

class TerminalWindow : public QMainWindow {
public:
    TerminalWindow(QWidget *parent = nullptr) : QMainWindow(parent) {
        QWidget *central = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(central);

        // Top panel: 3 horizontal sub-panels
        QHBoxLayout *topLayout = new QHBoxLayout();

        QGridLayout *bottomLayout = new QGridLayout();

        // For every layout you create, set margins and spacing
        topLayout->setContentsMargins(0, 0, 0, 0);
        topLayout->setSpacing(0);

        bottomLayout->setContentsMargins(0, 0, 0, 0);
        bottomLayout->setSpacing(0);

        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        QVector<QTermWidget*> terminals;

        for (int i = 0; i < 2; ++i) {
            QWidget *panel = new QWidget();
            QVBoxLayout *panelLayout = new QVBoxLayout(panel);
            panelLayout->setContentsMargins(1, 1, 1, 1);
            panelLayout->setSpacing(0);
            QTermWidget *term = new MyTerm();  configure_term(term, +1);
            panelLayout->addWidget(term);
            topLayout->addWidget(panel);
            terminals.append(term);  // Add to array
        }

        // Bottom panel: the AxB grid
        for (int row = 0; row < 2; ++row) {
            for (int col = 0; col < 3; ++col) {
                QWidget *panel = new QWidget();
                QVBoxLayout *panelLayout = new QVBoxLayout(panel);
                panelLayout->setContentsMargins(1, 1, 1, 1);
                panelLayout->setSpacing(0);
                QTermWidget *term = new MyTerm();  configure_term(term, -1);
                panelLayout->addWidget(term);
                bottomLayout->addWidget(panel, row, col);
                terminals.append(term);  // Add to array
            }
        }

        // Add top and bottom layouts to main layout
        QWidget *topWidget = new QWidget();
        topWidget->setLayout(topLayout);
        QWidget *bottomWidget = new QWidget();
        bottomWidget->setLayout(bottomLayout);

        mainLayout->addWidget(topWidget);
        mainLayout->addWidget(bottomWidget);

        setCentralWidget(central);

        // Optional: store terminals for later use
        this->terminalWidgets = terminals;
    }

private:
    QVector<QTermWidget*> terminalWidgets;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    TerminalWindow window;
    window.resize(800, 600);
    window.showMaximized();
    return app.exec();
}
