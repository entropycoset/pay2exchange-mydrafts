#include <QApplication>
#include <QMainWindow>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QVector>
#include <QMenu>
#include <QContextMenuEvent>
#include <QDir>
#include <QDebug>
#include <QTextEdit>
#include <QSplitter>
#include <QTimer>
#include <QDateTime>
#include <QStatusBar>
#include <QShortcut>
#include <QKeySequence>
#include <QShowEvent>
#include <QIcon>
#include <QPixmap>
#include <QMessageBox>
#include <QTabWidget>
#include <qtermwidget.h>
#include "myterm.h"
#include "loopbackfinder.h"

// Standard library includes
#include <array>
#include <string>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <random>
#include <chrono>
#include <filesystem>
#include <thread>
#include <fstream>
#include <unistd.h>

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



    template<typename... Args>
    std::string join_args(const std::string& sep, Args&&... args) {
        std::ostringstream oss;
        bool first = true;
        (void)std::initializer_list<int>{
            ((first ? (first = false, oss << std::forward<Args>(args))
                    : (oss << sep << std::forward<Args>(args))),
             0)...
          };
        return oss.str();
    }


}

namespace proj_count_ports {
    // wibecode
    // dumb and insecure grep

    int first_free_ipend_localhost(const int LOW, const int HIGH, bool debug=false) {
        LoopbackFinder finder;
        int result = finder.find_free(1000, 2000, 0);
        return result;
    }

    int first_free_ipend_localhost_old(const int LOW, const int HIGH, bool debug=false) {
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

/// settings for this simulation/word
struct WorldSettings {
    typedef decltype( std::time(nullptr) ) t_timestamp;
    const t_timestamp genesis_timestamp;
    std::string chaindid_str; ///< the ChainID but as string with hex

    std::string run_id() const {
        std::ostringstream oss;
        oss << std::time(nullptr) << "-" << getpid() << "-" << (rand() % 90000 + 10000);
        return oss.str();
    }

    WorldSettings(t_timestamp genesis_timestamp) : genesis_timestamp(genesis_timestamp) { }
};

struct TerminalWindowSettings {
    std::vector< std::string > program_args;
    int m_count_witness=0, m_count_user=0, m_count_all=0;
    int m_panes_big=2, m_panes_grid_A=1, m_panes_grid_B=1, m_panes_grid_all=1;

    TerminalWindowSettings(int argc, char **argv);

    std::string cfg_cwd, cfg_bc;
    int cfg_mynode, cfg_nodes_wit, cfg_nodes_user;
    std::string chainid;
    int main_ip_seg=-1; // the segment X in 127.0.0.X to be used as free IP for localhost for this world test
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

// Forward declarations
class TerminalPanel;

// StartupPanel - contains startup log and terminal
class StartupPanel : public QWidget {
    Q_OBJECT

private:
    QTextEdit *logText;
    MyTerm *startupTerm;
    std::shared_ptr<TerminalWindowSettings> m_settings;
    bool commandFinished;
    TerminalPanel *terminalPanel;

    std::shared_ptr<WorldSettings> world;

    std::filesystem::path chainid_fn; ///< at startup, in this file we will put the chainID
    std::filesystem::path chainid_sandboxdir; ///< the sandbox dir used by the instance that at startup gets the chainid

public:
    StartupPanel(std::shared_ptr<TerminalWindowSettings> settings, std::shared_ptr<WorldSettings> world, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_settings(settings) , world(world)
        , commandFinished(false)
        , terminalPanel(nullptr)
    {
        // main split: log / terminal
        QSplitter *splitter = new QSplitter(Qt::Vertical, this);

        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(5, 5, 5, 5);
        mainLayout->addWidget(splitter);

        // the log information
        logText = new QTextEdit();
        logText->setReadOnly(true);
        logText->setPlainText("Startup Log:\n");
        appendLog("Starting...");

        // terminal doing startup scripts
        startupTerm = new MyTerm();
        configure_term(startupTerm, 0);

        splitter->addWidget(logText);
        splitter->addWidget(startupTerm);
        splitter->setSizes({200, 400});

        connect(startupTerm, &QTermWidget::finished, this, &StartupPanel::onCommandFinished);
        appendLog("Layout ot startup.");
        runStartupCommand();
        appendLog("Startup constrcuted fully.");
    }

    void setTerminalPanel(TerminalPanel *panel) {
        terminalPanel = panel;
    }

signals:
    void startupComplete();

private slots:
    void onCommandFinished() {
        commandFinished = true;
        appendLog("Startup command completed.");
        appendLog("Proceeding to main application.");

        // TODO race condition? check.
        QTimer::singleShot(50, this, &StartupPanel::startupComplete);
    }

private:
    void appendLog(const std::string &message, int level=0) { this->appendLog(QString::fromStdString(message),level); }
    void appendLog(const char * message, int level=0) { this->appendLog(std::string(message),level); }
    void appendLog(const QString &message, int level=0) {
        std::cerr << "LOG: " << message.toStdString() << std::endl << std::flush ;
        QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        logText->append(QString("[%1] %2").arg(timestamp, message));

        logText->moveCursor(QTextCursor::End);

        //QTextCursor cursor = logText->textCursor();
        //cursor.movePosition(QTextCursor::End);
        logText->ensureCursorVisible();
        if (level>=2) {
            QMainWindow *mw = qobject_cast<QMainWindow*>(this->window());
            //assert(mw);
            if (mw) {
                mw->statusBar()->showMessage(message);
            }
        }
    }

    void runStartupCommand() {
        using std::string_literals::operator ""s;
        appendLog("Running startup commands...");

        appendLog("Running startup command to get ChainID");
        std::vector<std::string> args;
        std::string cmd = "p2e-dev-node1";
        int userindex=0;
        args.push_back("normal");
        args.push_back(this->m_settings->cfg_bc);
        args.push_back("chainid");
        args.push_back("-userindex="s + std::to_string(userindex));
        args.push_back("-initts="s + std::to_string(world->genesis_timestamp));
        const auto runid = world->run_id();
        args.push_back("-runsubdir="s + runid);
        args.push_back("-pause");

        // file name such as 1759503895-329778-69383/var-chainid_0 :
        this->chainid_sandboxdir = std::filesystem::path("run") / std::filesystem::path( runid )
            / std::filesystem::path( std::string("var-") + std::string("chainid_") + std::to_string(userindex) ) ;
        this->chainid_fn = chainid_sandboxdir / "chainid.txt" ;
        std::ostringstream oss;
        oss << "Will use chainid_fn=[" << chainid_fn << "] " << " while in CWD=[" << std::filesystem::current_path() << "] \n";
        oss << "Will run command [" << cmd << "] with args: ";
        for (const auto & arg : args) { oss << "[" << arg << "]" << " "; }
        oss << "\n";
        appendLog(oss.str());
        startupTerm->run_cmd(cmd, args);
        appendLog("Command will be run in term...");

        // unsafe race against death of *this ... TODO (upgrade this to live as shared_ptr? or?)
        QTimer::singleShot(50, this, &StartupPanel::next_step_chainid);
    }

    void next_step_chainid();
};

// TerminalPanel - contains all the terminal grid content
class TerminalPanel : public QWidget {
    Q_OBJECT

protected:
    std::shared_ptr<TerminalWindowSettings> m_settings;
    QVector<MyTerm*> terminals; ///< the terminal widgets. these are Qt-like objects, they are owned (memory) by the GUI parent etc
    QVector<MyTerm*> terminals_node_any; ///< the terminal widgets that leads to any-node with number N+1 (+1 since numbering is from 1). these are Qt-like objects, they are owned (memory) by the GUI parent etc
    bool commandsStarted = false;
    std::shared_ptr<WorldSettings> world;

public:
    TerminalPanel(std::shared_ptr<TerminalWindowSettings> settings, std::shared_ptr<WorldSettings> _world, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_settings(settings)
        , world(_world)
    {
        QVBoxLayout *mainLayout = new QVBoxLayout(this);

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

        for (int i = 0; i < this->m_settings->m_panes_big; ++i) {
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
        for (int row = 0; row < this->m_settings->m_panes_grid_B; ++row) {
            for (int col = 0; col < this->m_settings->m_panes_grid_A; ++col) {
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
    }

    void start_commands(int step) {
        //if (commandsStarted) return;
        //commandsStarted = true;

        this->m_settings->main_ip_seg = proj_count_ports::first_free_ipend_localhost(100,2000,true);
        const int free_ipend = this->m_settings->main_ip_seg;

        using namespace std::string_literals;

        int countTerm=0, countNode=0, countNodeWitt=0, countNodeUser=0; // counter: terminals, node(any type), witness node, user nodes
        int countWallet=50; // also counter of wallets. (start number for testing/visibility)
        for (MyTerm * & term : terminals) {

            this->terminals_node_any.push_back(term);
            enum { is_wit, is_usr, is_wallet, is_shell } role = is_shell;

            if (countNodeUser < m_settings->m_count_user) role = is_usr;
            if (countNodeWitt < m_settings->m_count_witness) role = is_wit; // wit01 and such
            if (countTerm == 1) role = is_wallet;

            std::vector<std::string> args;
            std::string cmd;
            bool cmd_run_it=true;

            bool added_witt = false; // we created wittness now?
            bool added_user = false; // we created regular-user now?
            bool added_wallet = false; // we created wallet client now?
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
                    added_wallet = true;
                    break;
                }
                case is_shell: {
                    role_str = "shell";
                    cmd_run_it=false;

                    break;
                }
            }
            bool skip=false;
            switch (step) {
                case 1: { if (role==is_wallet) skip=true; } break;
                case 2: { if (role!=is_wallet) skip=true; } break;
            }


            if ((role == is_wallet) && (step == 1)) {
                term->run_cmd("echo"s, {"Wait... (getting ChainID)"});
 //               term->run_cmd(QString::fromStdString("echo"),   );
            }

            if (! skip) {
                int portindex = countNode;
                int portrpc = 1025 + portindex*2 +0;
                int portp2p = 1025 + portindex*2 +1;

            if ((role == is_wit)||(role == is_usr)) {

                term->run_cmd("sleep"s , {"1"s});

                args.push_back("normal");
                args.push_back(this->m_settings->cfg_bc);
                args.push_back(role_str);
                //args.push_back("-portindex="s + std::to_string(countNode));
                args.push_back("-portp2p="s + std::to_string(portp2p));
                args.push_back("-portrpc="s + std::to_string(portrpc));

                args.push_back("-userindex="s + std::to_string(countNode));
                args.push_back("-initts="s + std::to_string(world->genesis_timestamp));
                args.push_back("-runsubdir="s + (world->run_id()));
                const std::string netip = [free_ipend]() -> std::string { std::ostringstream oss; oss << "127.0.0." << free_ipend; return oss.str(); }();
                std::cerr<<"Will use netip=" << netip << std::endl;
                args.push_back("-netip="s + netip);
                args.push_back("-e"); // === below are args passed to the node program directly ===
                args.push_back("--seed-nodes=[\"" + netip + ":1026\", \"" + netip + ":1028\"]");
                //args.push_back("--exit");
                //args.push_back("--minimal");
                // args.push_back("--net-reuse");
                //"  witness nr 1 -> p2e-dev-node1 normal ec2 wit01 -portindex=1  -userindex=1\n"
            }
            else if (role == is_wallet) {
                // run script:   p2e-wallet n ../../../program/wallet_cli/wallet_cli -runsubdir=moo
                // with options (after -e) :
                //  --server-rpc-endpoint=ws://127.0.0.2:1025  --chain-id 996620da58efeea04536c86f23e3490b683381d685716ae02aeee5d29fe69916  --rpc-http-endpoint
                const std::string use_node_rpc_host = "127.0.0."s + std::to_string(free_ipend);
                const int use_node_rpc_port = portrpc;

                const auto binary_cmd1 = "./programs/cli_wallet/cli_wallet";
                const auto binary_cmd2 = "../../.."s + binary_cmd1;
                cmd = "p2e-wallet" ; // out wrapper script

                args.push_back("n"); // our wallet script: n for normal run
                args.push_back(binary_cmd2); // our wallet script: the wallet binary to run
                args.push_back("-userindex="s + std::to_string(countWallet));
                args.push_back("-runsubdir="s + (world->run_id()));

                args.push_back("-wait_node_ip="s + use_node_rpc_host);
                args.push_back("-wait_node_port="s + std::to_string(use_node_rpc_port));

                args.push_back("-e"); // now after -e the options passed on to actuall wallet binary:
                args.push_back( [&](){ std::ostringstream oss;
                    oss<<"--server-rpc-endpoint=ws://"<<use_node_rpc_host<<":"<<use_node_rpc_port; return oss.str(); }() );

                args.push_back("--chain-id");
                args.push_back(this->m_settings->chainid);
                //args.push_back( [&](){ std::ostringstream oss; oss<<"--chain-id" << ' ' << this->m_settings.chainid; return oss.str(); }() );

                args.push_back("--rpc-http-endpoint");
                //args.push_back( [&](){ std::ostringstream oss; oss<<"--rpc-http-endpoint" ; return oss.str(); }() );
            }
            } // not skipped

            if (added_witt) countNodeWitt++;
            if (added_user) countNodeUser++;
            if (added_user || added_witt) countNode++;
            if (added_wallet) countWallet++;
            const auto thisTermIx = countTerm;
            countTerm++;

            if (!skip) {
            std::ostringstream info_oss;
            info_oss << "Term #"<<(thisTermIx)<< std::left << " role=" << std::setw(6) << role_str
                     << " node=nr-" << countNode  << " wit=nr-" << countNodeWitt << " usr=nr-"<< countNodeUser
                     << "." << std::right;
            term->run_cmd(std::string("echo "), {  info_oss.str() } );

            if (cmd_run_it) term->run_cmd(QString::fromStdString(cmd), args );
            }
        }
    }

protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);

        // Schedule command execution after the widget is fully shown and sized
        if (!commandsStarted) {
            // Use a timer to ensure the layout is fully complete
            QTimer::singleShot(0, this, [this](){ TerminalPanel::start_commands(1); } ); // TODO race conditions? check.
        }
    }
};

// SimulationPanel - contains the tab widget with both startup and terminal panels
class SimulationPanel : public QWidget {
    Q_OBJECT

private:
    QTabWidget *tabWidget;
    StartupPanel *startupPanel;
    TerminalPanel *terminalPanel;
    std::shared_ptr<TerminalWindowSettings> m_settings;

public:
    SimulationPanel(std::shared_ptr<TerminalWindowSettings> settings, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_settings(settings)
    {
        // Create main layout
        QVBoxLayout *mainLayout = new QVBoxLayout(this);
        mainLayout->setContentsMargins(0, 0, 0, 0);
        mainLayout->setSpacing(0);

        auto world = std::make_shared<WorldSettings>(std::time(nullptr));

        // Create tab widget
        tabWidget = new QTabWidget(this);
        mainLayout->addWidget(tabWidget);

        // Create and add startup panel to tab #1
        startupPanel = new StartupPanel(m_settings, world);
        tabWidget->addTab(startupPanel, "Startup");

        // Create and add terminal panel to tab #2

        terminalPanel = new TerminalPanel(m_settings, world);
        tabWidget->addTab(terminalPanel, "Terminal Grid");
        tabWidget->setCurrentIndex(1);

        // Connect startup completion signal to switch tabs
        connect(startupPanel, &StartupPanel::startupComplete, this, &SimulationPanel::onStartupComplete);

        // Set terminal panel reference in startup panel
        startupPanel->setTerminalPanel(terminalPanel);
    }

private slots:
    void onStartupComplete() {
        // Switch to terminal grid tab when startup is complete
        tabWidget->setCurrentIndex(1);
    }
};

// SimulationWindow - the main window containing the simulation panel
class SimulationWindow : public QMainWindow {
    Q_OBJECT

private:
    SimulationPanel *simulationPanel;
    std::shared_ptr<TerminalWindowSettings> m_settings;

public:
    SimulationWindow(std::shared_ptr<TerminalWindowSettings> settings, QWidget *parent = nullptr)
        : QMainWindow(parent)
        , m_settings(settings)
    {
        setWindowTitle("Grid Terminal Simulation");
        resize(1024, 768);

        // Create and set the simulation panel as central widget
        simulationPanel = new SimulationPanel(m_settings);
        setCentralWidget(simulationPanel);

        // Setup keyboard shortcuts
        new QShortcut(QKeySequence("F9"), this, SLOT(nice_shutdown()));
    }

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

public slots:
    void nice_shutdown() {
        this->close();
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
    std::shared_ptr<TerminalWindowSettings> settings = std::make_shared<TerminalWindowSettings>(argc,argv);

    // Create and show simulation window with tabs
    SimulationWindow *simulationWindow = new SimulationWindow(settings);
    simulationWindow->showMaximized();

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

void StartupPanel::next_step_chainid() {
    bool ready = std::filesystem::exists(chainid_fn);
    if (!ready) {
        appendLog("Still waiting for the file to be created.");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        QTimer::singleShot(250, this, &StartupPanel::next_step_chainid);
    }
    else {
        appendLog("Got the file with chainid");
        {
            std::ifstream ifs(chainid_fn);
            std::string chainid;
            std::getline(ifs, chainid);
            this->m_settings->chainid = chainid;
            appendLog("Got chainid=[" + chainid + "]", 2);
            //this->terminalPanel->start_commands(2);
            QTimer::singleShot(50, this, [this](){ this->terminalPanel->start_commands(2); } ); // TODO race conditions? check.
        }
        { // unpause the node that calculated sandbox, so it can die now
            auto const unpause_fn = this->chainid_sandboxdir / "unpause";
            std::ofstream ofs(unpause_fn);
        }
    }
}
