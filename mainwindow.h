#pragma once

#include <QList>
#include <QMainWindow>

#include "article.h"
#include "pubmedclient.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QModelIndex;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTextBrowser;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void startSearch();
    void goPrevious();
    void goNext();
    void onSearchFinished(int total, const QStringList &pmids, const QString &translatedQuery);
    void onArticlesReady(const QList<Article> &articles);
    void onFailed(const QString &message);
    void onBusyChanged(bool busy);
    void showDetails(const QModelIndex &current);
    void openInBrowser(const QModelIndex &index);
    void exportCsv();
    void exportPmids();
    void editSettings();
    void about();

private:
    void buildUi();
    void buildMenus();
    void loadSettings();
    void runSearch();
    void updatePaging();
    QString buildTerm() const;

    static constexpr int kPageSize = 50;
    static constexpr int kMaxResults = 10000; // PubMed shows at most 10,000 results

    PubMedClient m_client;
    QList<Article> m_articles;
    QString m_term;
    QString m_sort;
    int m_start = 0;
    int m_total = 0;
    bool m_busy = false;

    QLineEdit *m_query = nullptr;
    QPushButton *m_searchButton = nullptr;
    QComboBox *m_dateCombo = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QComboBox *m_sortCombo = nullptr;
    QCheckBox *m_freeFullText = nullptr;
    QCheckBox *m_hasAbstract = nullptr;
    QTableView *m_table = nullptr;
    QStandardItemModel *m_model = nullptr;
    QTextBrowser *m_detail = nullptr;
    QPushButton *m_prevButton = nullptr;
    QPushButton *m_nextButton = nullptr;
    QLabel *m_info = nullptr;
};
