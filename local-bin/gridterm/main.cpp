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
#include <QDir>
#include <QDebug>
#include <iostream>
#include <iomanip>
#include <sstream>

#include <QShortcut>
#include <QKeySequence>

#include "myterm.h"

QString expandTilde(const QString &path) {
    if (path == "~") { return QDir::homePath(); }
    if (path.startsWith("~/")) { return QDir::homePath() + path.mid(1); }
    return path;
}



std::string usage() {
    return "program CWD BLOCKCHAIN MYNODE NODES_COUNT_WIT NODES_COUNT_USER \n"
        "  CWD - like ~/foo/ - the directory to change into at start \n"
        "  {TODO} BLOCKCHAIN - like ec2 - the name of blockchain to load \n"
        "  {TODO} MYNODE - number of main node to view, e.g. 1 for wit01 to focus on (bigger panel?)\n"
        "  {TODO} NODES_COUNT_WIT - override number of panes (terms) and node to try to start - witness nodes (mining keys) \n"
        "  {TODO} NODES_COUNT_USER - override number of panes (terms) and node to try to start - regular user nodes (no mining keys) \n"
        " \n"
        "example: \n"
        "program ~/code/core/pay2exchange-core ec2 1 \n"
        "program ~/code/core/pay2exchange-core ec2 1 5 \n"
        "program ~/code/core/pay2exchange-core ec2 1 7 3 \n"
        "  this last example will start: \n"
        "  witness nr 1 -> p2e-dev-node1 normal ec2 wit01 -portindex=1  -userindex=1\n"
        "  witness nr 7 -> p2e-dev-node1 normal ec2 wit07 -portindex=7  -userindex=7\n"
        "  user    nr 1 -> p2e-dev-node1 normal ec2 node  -portindex=8  -userindex=8\n"
        "  user    nr 3 -> p2e-dev-node1 normal ec2 node  -portindex=10 -userindex=10\n"
        "  (or similar auto-assigned indexes for port and user) \n"
    ;
}

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

struct TerminalWindowSettings {
    std::vector< std::string > program_args;
    int m_count_witness=0, m_count_user=0, m_count_all=0;
    int m_panes_big=2, m_panes_grid_A=1, m_panes_grid_B=1, m_panes_grid_all=1;

    TerminalWindowSettings(int argc, char **argv);

    std::string cfg_cwd, cfg_bc;
    int cfg_mynode, cfg_nodes_wit, cfg_nodes_user;
};

class TerminalWindow : public QMainWindow {
protected:
    TerminalWindowSettings m_settings;
    QVector<MyTerm*> terminals; ///< the terminal widgets. these are Qt-like objects, they are owned (memory) by the GUI parent etc
    QVector<MyTerm*> terminals_node_any; ///< the terminal widgets that leads to any-node with number N+1 (+1 since numbering is from 1). these are Qt-like objects, they are owned (memory) by the GUI parent etc

public:

    bool eventFilter(QObject *obj, QEvent *event) {
        if (event->type() == QEvent::KeyPress) {
            QKeyEvent *key = static_cast<QKeyEvent*>(event);
            if ( ((key->key() == Qt::Key_Escape && key->modifiers() == Qt::AltModifier))
                || (key->key() == Qt::Key_F9) )
            {
                nice_shutdown();
                return true; // event handled
            }
        }
        return QMainWindow::eventFilter(obj, event);
    }

    void nice_shutdown() {
        this->close();
    }

    TerminalWindow(TerminalWindowSettings settings, QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_settings(settings)
    {
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

        const int count_nodes_into_big=1; // how many of the nodes will be placed actually into the big panes
        for (int i = 0; i < this->m_settings.m_panes_big; ++i) {
            QWidget *panel = new QWidget();
            QVBoxLayout *panelLayout = new QVBoxLayout(panel);
            panelLayout->setContentsMargins(1, 1, 1, 1);
            panelLayout->setSpacing(0);
            MyTerm *term = new MyTerm();  configure_term(term, +1);
            panelLayout->addWidget(term);
            topLayout->addWidget(panel);
            terminals.append(term);  // Add to array
        }

        // Bottom panel: the AxB grid
        for (int row = 0; row < this->m_settings.m_panes_grid_B; ++row) {
            for (int col = 0; col < this->m_settings.m_panes_grid_A; ++col) {
                QWidget *panel = new QWidget();
                QVBoxLayout *panelLayout = new QVBoxLayout(panel);
                panelLayout->setContentsMargins(1, 1, 1, 1);
                panelLayout->setSpacing(0);
                MyTerm *term = new MyTerm();  configure_term(term, -1);
                panelLayout->addWidget(term);
                bottomLayout->addWidget(panel, row, col);
                terminals.append(term);  // Add to array
            }
        }
//                              { QString::fromStdString( std::to_string(nodeNum) ) } );

        // Add top and bottom layouts to main layout
        QWidget *topWidget = new QWidget();
        topWidget->setLayout(topLayout);
        QWidget *bottomWidget = new QWidget();
        bottomWidget->setLayout(bottomLayout);

        mainLayout->addWidget(topWidget);
        mainLayout->addWidget(bottomWidget);

        setCentralWidget(central);

        new QShortcut(QKeySequence("F9"), this, SLOT(nice_shutdown()));

        using namespace std::string_literals;

        int countTerm=0, countNode=0, countNodeWitt=0, countNodeUser=0; // counter: terminals, node(any type), witness node, user nodes
        for (const auto & term : terminals) {

            this->terminals_node_any.push_back(term);
            std::ostringstream info_oss; info_oss << "Term #"<<(countTerm);
            term->run_cmd("echo ", {  info_oss.str() } );

            std::vector<std::string> args;
            args.push_back("normal");
            args.push_back("ec2");
            bool added_witt=false; // we created wittness now?
            bool added_user=false; // we created regular-user now?
            std::string role;
            if (countNode < m_settings.m_count_witness) { // wit01 and such
                int numWitt = countNodeWitt + 1; // numbers of witness is from 1, such as wit01
                std::ostringstream oss; oss<<"wit"<<std::setw(2)<<std::setfill('0')<<numWitt;
                role = oss.str();
                added_witt=true;
            } else { // user
                role="node";
                added_user=true;
            }
            args.push_back(role);
            args.push_back( "-portindex="s + std::to_string(countNode));
            args.push_back( "-userindex="s + std::to_string(countNode));
            args.push_back( "-netip="s + "127.0.0.1"s);
            term->run_cmd("p2e-dev-node1", args );
            //"  witness nr 1 -> p2e-dev-node1 normal ec2 wit01 -portindex=1  -userindex=1\n"
            if (added_witt) countNodeWitt++;
            if (added_user) countNodeUser++;
            if (added_user || added_witt) countNode++;
            countTerm++;
        }

    }
};

int main(int argc, char *argv[]) {

    if (argc > 1) {
        QString dirPath_given = QString::fromLocal8Bit(argv[1]);
        QString dirPath_exp = expandTilde(dirPath_given);
        if (!QDir::setCurrent(dirPath_exp)) {
            qWarning() << "Failed to change working directory to" << dirPath_exp;
        } else {
            std::cout << "Changed working dir into " << dirPath_exp.toStdString() << "\n";

        }
    }

    QApplication app(argc, argv);
    TerminalWindowSettings settings(argc,argv);
    TerminalWindow window(settings);
    app.installEventFilter(&window);
    window.resize(800, 600);
    window.showMaximized();
    return app.exec();
}

TerminalWindowSettings::TerminalWindowSettings(int argc, char **argv) {
    this->program_args.assign(argv, argv + argc);

    // program CWD BLOCKCHAIN MYNODE NODES_COUNT_WIT NODES_COUNT_USER
    // (see usage function)
    try {
    this->cfg_cwd = program_args.at(1);
    this->cfg_bc = program_args.at(2);
    this->cfg_mynode = std::stoi(program_args.at(3));
    this->cfg_nodes_wit = std::stoi(program_args.at(4));
    this->cfg_nodes_user = std::stoi(program_args.at(5));
    } catch(std::exception ex) {
        std::cout << usage();
        throw;
    }

    m_count_witness = cfg_nodes_wit;
    m_count_user = cfg_nodes_user;
    m_count_all = m_count_witness + m_count_user;

    m_panes_big = 2;
    int grid_needs = m_count_all - 1; // -1 since one nodes go to big panel
    if (grid_needs<=1) { m_panes_grid_A=1;  m_panes_grid_B=1; }
    else if (grid_needs<=2) { m_panes_grid_A=1;  m_panes_grid_B=2; }
    else if (grid_needs<=4) { m_panes_grid_A=2;  m_panes_grid_B=2; }
    else if (grid_needs<=6) { m_panes_grid_A=3;  m_panes_grid_B=2; }
    else if (grid_needs<=9) { m_panes_grid_A=3;  m_panes_grid_B=3; }
    else if (grid_needs<=12) { m_panes_grid_A=4;  m_panes_grid_B=3; }
    else if (grid_needs<=16) { m_panes_grid_A=4;  m_panes_grid_B=4; }
    else { m_panes_grid_A=7;  m_panes_grid_B=5; } // whoah ok there big boy, slow down
    m_panes_grid_all = m_panes_grid_A * m_panes_grid_B ;
}
