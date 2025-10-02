#include <QApplication>
#include <array>
#include <string>
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
#include <ctime>
#include <cstring>
#include <QTextEdit>
#include <QSplitter>
#include <QTimer>
#include <QDateTime>

#include <QShortcut>
#include <QKeySequence>

#include "myterm.h"
#include <QIcon>
#include <QPixmap>

#include <unistd.h>    // For getpid()
#include <cstdlib>     // For rand()
#include <ctime>       // For std::time()
#include <sstream>     // For std::ostringstream (oss)

#include <random>
#include <chrono>

#include <QMessageBox>

#include <QSplitter>
#include <QTextEdit>
#include <QDateTime>
#include <QTimer>


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

namespace utils {
    std::mt19937& get_random_engine() {
        static std::random_device rd;
        static std::mt19937 mt(rd() ^
            std::chrono::system_clock::now().time_since_epoch().count());
        return mt;
    }
}

namespace proj_count_ports {
    // wibecode
    // dumb and insecure grep

    int first_free_ipend_localhost(const int LOW, const int HIGH, bool debug=false) {
        std::array<bool, 256> occ{};
        occ.fill(false);

        // Run `ss -tnlH` to list listening TCP sockets
        FILE* f = popen("ss -tnlH 2>/dev/null", "r");
        if (!f) {
            std::cerr << "Failed to run ss\n";
            return 2;
        }

        char buf[4096];
        while (fgets(buf, sizeof(buf), f)) {
            std::string line(buf);

            // Tokenize by whitespace
            std::istringstream iss(line);
            std::string token;
            while (iss >> token) {
                if (token.find("127.0.0.") == std::string::npos)
                    continue;

                // Clean trailing characters like commas/brackets
                while (!token.empty() && !std::isdigit(token.back()))
                    token.pop_back();

                int a, b, c, d, port;
                if (sscanf(token.c_str(), "%d.%d.%d.%d:%d",
                           &a, &b, &c, &d, &port) == 5) {
                    if (a == 127 && b == 0 && c == 0) {
                        if (port >= LOW && port <= HIGH && d >= 0 && d <= 255) {
                            occ[d] = true;
                        }
                    }
                           }
            }
        }
        pclose(f);

        for (int x = 1; x <= 254; ++x) {
            if (occ[x]) {
                if (debug) std::cerr << "TAKEN IP:" << x << std::endl;
            }
        }

        // Print all free X values
        int one_free=-1;
        std::cerr << "Below are free IPs: \n";
        for (int x = 1; x <= 254; ++x) {
            if (!occ[x]) {
                if (debug) std::cout << x << std::endl;
                one_free = x;
                break ;
            }
        }
        return one_free;
    }
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

    
void error_gui(const QString& message) {
    QMessageBox msgBox;
    msgBox.setIcon(QMessageBox::Critical);
    msgBox.setWindowTitle("Error");
    msgBox.setText(message);
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Abort);
    msgBox.setDefaultButton(QMessageBox::Ok);

    int result = msgBox.exec();

    if (result == QMessageBox::Abort) {
        QApplication::quit();
    }
}

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

        // TODO: logical
        int genesis_timestamp = std::time(nullptr) - 10;

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

        const int free_ipend = proj_count_ports::first_free_ipend_localhost(100,2000,true);
        const std::string run_id = []() {
            std::ostringstream oss;
            oss << std::time(nullptr) << "-" << getpid() << "-" << (rand() % 90000 + 10000);
            return oss.str();
        }();

        new QShortcut(QKeySequence("F9"), this, SLOT(nice_shutdown()));

        using namespace std::string_literals;

        int countTerm=0, countNode=0, countNodeWitt=0, countNodeUser=0; // counter: terminals, node(any type), witness node, user nodes
        for (const auto & term : terminals) {

            this->terminals_node_any.push_back(term);
            enum { is_wit, is_usr, is_wallet, is_shell } role = is_shell;

            if (countNodeUser < m_settings.m_count_user) role = is_usr;
            if (countNodeWitt < m_settings.m_count_witness) role = is_wit; // wit01 and such
            if (countTerm == 1) role = is_wallet;

            std::vector<std::string> args;
            std::string cmd;
            bool cmd_run_it=true;

            bool added_witt = false; // we created wittness now?
            bool added_user = false; // we created regular-user now?
            std::string role_str;
            switch (role) {
                case is_wit: {
                    cmd = "p2e-dev-node1";
                    int numWitt = countNodeWitt + 1; // numbers of witness is from 1, such as wit01
                    std::ostringstream oss;
                    oss << "wit" << std::setw(2) << std::setfill('0') << numWitt;
                    role_str = oss.str();
                    added_witt = true;
                    break;
                }
                case is_usr: {
                    cmd = "p2e-dev-node1";
                    role_str = "node";
                    added_user = true;
                    break;
                }
                case is_wallet: {
                    cmd = "bash";
                    role_str = "wallet";
                    break;
                }
                case is_shell: {
                    role_str = "shell";
                    cmd_run_it=false;

                    break;
                }
            }

            if ((role == is_wit)||(role == is_usr)) {
                args.push_back("normal");
                args.push_back(this->m_settings.cfg_bc);
                args.push_back(role_str);
                args.push_back("-portindex="s + std::to_string(countNode));

                args.push_back("-userindex="s + std::to_string(countNode));
                args.push_back("-initts="s + std::to_string(genesis_timestamp));
                args.push_back("-runsubdir="s + (run_id));
                const std::string netip = [free_ipend]() -> std::string { std::ostringstream oss; oss << "127.0.0." << free_ipend; return oss.str(); }();
                std::cerr<<"Will use netip=" << netip << std::endl;
                args.push_back("-netip="s + netip);

                args.push_back("-e"); // === bewlo are args passed to the node program directly ===
                args.push_back("--seed-nodes=[\"" + netip + ":1026\", \"" + netip + ":1028\"]");
                args.push_back("--exit");
                args.push_back("--minimal");
                // args.push_back("--net-reuse");
                //"  witness nr 1 -> p2e-dev-node1 normal ec2 wit01 -portindex=1  -userindex=1\n"
            }

            if (added_witt) countNodeWitt++;
            if (added_user) countNodeUser++;
            if (added_user || added_witt) countNode++;
            const auto thisTermIx = countTerm;
            countTerm++;

            std::ostringstream info_oss;
            info_oss << "Term #"<<(thisTermIx)<< std::left << " role=" << std::setw(6) << role_str
                     << " node=nr-" << countNode  << " wit=nr-" << countNodeWitt << " usr=nr-"<< countNodeUser
                     << "." << std::right;
            term->run_cmd("echo ", {  info_oss.str() } );

            if (cmd_run_it) term->run_cmd(QString::fromStdString(cmd), args );
        }

    }
};

class StartupWindow : public QMainWindow {
    Q_OBJECT

private:
    QTextEdit *logText;
    MyTerm *startupTerm;
    TerminalWindowSettings m_settings;
    bool commandFinished;

public:
    StartupWindow(TerminalWindowSettings settings, QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_settings(settings)
        , commandFinished(false)
    {
        setWindowTitle("Startup - QtQuitButton");
        resize(800, 600);

        QWidget *central = new QWidget(this);
        setCentralWidget(central);

        // Create a vertical splitter to divide top and bottom
        QSplitter *splitter = new QSplitter(Qt::Vertical, central);
        
        // Create main layout
        QVBoxLayout *mainLayout = new QVBoxLayout(central);
        mainLayout->setContentsMargins(5, 5, 5, 5);
        mainLayout->addWidget(splitter);

        // Top part - Read-only text log
        logText = new QTextEdit();
        logText->setReadOnly(true);
        logText->setPlainText("Startup Log:\n");
        logText->append("Initializing startup sequence...");
        
        // Bottom part - Terminal
        startupTerm = new MyTerm();
        configure_term(startupTerm, 0);
        
        // Add widgets to splitter
        splitter->addWidget(logText);
        splitter->addWidget(startupTerm);
        
        // Set initial sizes - give more space to terminal
        splitter->setSizes({200, 400});

        // Connect terminal finished signal to our slot
        connect(startupTerm, &QTermWidget::finished, this, &StartupWindow::onCommandFinished);
        
        // Log startup info
        appendLog("Startup window created");
        appendLog("Ready to run startup command...");
        
        // Run your startup command here
        runStartupCommand();
    }

private slots:
    void onCommandFinished() {
        commandFinished = true;
        appendLog("Startup command completed!");
        appendLog("Proceeding to main application...");
        
        // Wait a moment then proceed to main window
        QTimer::singleShot(1000, this, &StartupWindow::proceedToMainWindow);
    }

    void proceedToMainWindow() {
        appendLog("Launching main terminal grid...");
        
        // Create and show the main terminal window
        TerminalWindow *mainWindow = new TerminalWindow(m_settings);
        mainWindow->resize(800, 600);
        mainWindow->showMaximized();
        
        // Close this startup window
        this->close();
    }

private:
    void appendLog(const QString &message) {
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
        logText->append(QString("[%1] %2").arg(timestamp, message));
        logText->ensureCursorVisible();
    }

    void runStartupCommand() {
        appendLog("Running startup command...");
        
        // Example startup command - modify this to your needs
        // You can run any command you want here
        startupTerm->run_cmd("bash", {"-c", "your-initialization-script.sh"}); // TODO
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
    
    // Create and show startup window instead of main window directly
    StartupWindow *startupWindow = new StartupWindow(settings);
    startupWindow->show();
    
    return app.exec();
}

// Include the MOC file for the Q_OBJECT macro to work
#include "main.moc"

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

    //m_panes_grid_all = m_panes_grid_A * m_panes_grid_B ;
}
