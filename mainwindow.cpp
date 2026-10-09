#include "mainwindow.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QStringConverter>
#include <QTableView>
#include <QTextBrowser>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString csvField(const QString &s)
{
    QString t = s;
    t.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + t + QLatin1Char('"');
}

QString shortAuthors(const QStringList &authors)
{
    if (authors.size() <= 3)
        return authors.join(QStringLiteral(", "));
    return authors.mid(0, 3).join(QStringLiteral(", ")) + QStringLiteral(" et al.");
}

QString pubmedUrl(const QString &pmid)
{
    return QStringLiteral("https://pubmed.ncbi.nlm.nih.gov/") + pmid + QLatin1Char('/');
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(tr("PubMed Search"));
    resize(1050, 740);

    buildUi();
    buildMenus();
    loadSettings();

    connect(&m_client, &PubMedClient::searchFinished, this, &MainWindow::onSearchFinished);
    connect(&m_client, &PubMedClient::articlesReady, this, &MainWindow::onArticlesReady);
    connect(&m_client, &PubMedClient::failed, this, &MainWindow::onFailed);
    connect(&m_client, &PubMedClient::busyChanged, this, &MainWindow::onBusyChanged);

    updatePaging();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);

    // Row 1: query box + Search
    auto *queryRow = new QHBoxLayout;
    m_query = new QLineEdit;
    m_query->setPlaceholderText(
        tr("Search PubMed - e.g.  asthma AND children   or   smith j[au] AND copd[ti]"));
    m_query->setClearButtonEnabled(true);
    m_searchButton = new QPushButton(tr("Search"));
    m_searchButton->setDefault(true);
    queryRow->addWidget(m_query, 1);
    queryRow->addWidget(m_searchButton);
    root->addLayout(queryRow);

    // Row 2: simple filters
    auto *filterRow = new QHBoxLayout;

    m_dateCombo = new QComboBox;
    m_dateCombo->addItem(tr("Any time"), QString());
    m_dateCombo->addItem(tr("Last year"), QStringLiteral("\"last 1 years\"[dp]"));
    m_dateCombo->addItem(tr("Last 5 years"), QStringLiteral("\"last 5 years\"[dp]"));
    m_dateCombo->addItem(tr("Last 10 years"), QStringLiteral("\"last 10 years\"[dp]"));

    m_typeCombo = new QComboBox;
    m_typeCombo->addItem(tr("Any type"), QString());
    m_typeCombo->addItem(tr("Clinical trial"), QStringLiteral("\"Clinical Trial\"[pt]"));
    m_typeCombo->addItem(tr("Randomized controlled trial"),
                         QStringLiteral("\"Randomized Controlled Trial\"[pt]"));
    m_typeCombo->addItem(tr("Review"), QStringLiteral("\"Review\"[pt]"));
    m_typeCombo->addItem(tr("Systematic review"), QStringLiteral("systematic[sb]"));
    m_typeCombo->addItem(tr("Meta-analysis"), QStringLiteral("\"Meta-Analysis\"[pt]"));

    m_sortCombo = new QComboBox;
    m_sortCombo->addItem(tr("Best match"), QStringLiteral("relevance"));
    m_sortCombo->addItem(tr("Publication date"), QStringLiteral("pub_date"));
    m_sortCombo->addItem(tr("First author"), QStringLiteral("Author"));
    m_sortCombo->addItem(tr("Journal"), QStringLiteral("JournalName"));

    m_freeFullText = new QCheckBox(tr("Free full text"));
    m_hasAbstract = new QCheckBox(tr("Has abstract"));

    filterRow->addWidget(new QLabel(tr("Date:")));
    filterRow->addWidget(m_dateCombo);
    filterRow->addWidget(new QLabel(tr("Type:")));
    filterRow->addWidget(m_typeCombo);
    filterRow->addWidget(new QLabel(tr("Sort:")));
    filterRow->addWidget(m_sortCombo);
    filterRow->addWidget(m_freeFullText);
    filterRow->addWidget(m_hasAbstract);
    filterRow->addStretch(1);
    root->addLayout(filterRow);

    // Results table + abstract pane
    m_model = new QStandardItemModel(0, 5, this);
    m_model->setHorizontalHeaderLabels(
        {tr("Year"), tr("Title"), tr("Authors"), tr("Journal"), tr("PMID")});

    m_table = new QTableView;
    m_table->setModel(m_model);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setColumnWidth(0, 55);
    m_table->setColumnWidth(1, 430);
    m_table->setColumnWidth(2, 220);
    m_table->setColumnWidth(3, 190);
    m_table->setColumnWidth(4, 80);

    m_detail = new QTextBrowser;
    m_detail->setOpenExternalLinks(true);
    m_detail->setPlaceholderText(tr("Select a result to read its abstract. Double-click to open it in PubMed."));

    auto *splitter = new QSplitter(Qt::Vertical);
    splitter->addWidget(m_table);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    root->addWidget(splitter, 1);

    // Paging row
    auto *pageRow = new QHBoxLayout;
    m_prevButton = new QPushButton(tr("< Previous"));
    m_nextButton = new QPushButton(tr("Next >"));
    m_info = new QLabel(tr("Enter a search to begin."));
    m_info->setAlignment(Qt::AlignCenter);
    pageRow->addWidget(m_prevButton);
    pageRow->addWidget(m_info, 1);
    pageRow->addWidget(m_nextButton);
    root->addLayout(pageRow);

    setCentralWidget(central);
    statusBar()->showMessage(tr("Data from PubMed (National Library of Medicine)."));

    connect(m_searchButton, &QPushButton::clicked, this, &MainWindow::startSearch);
    connect(m_query, &QLineEdit::returnPressed, this, &MainWindow::startSearch);
    connect(m_prevButton, &QPushButton::clicked, this, &MainWindow::goPrevious);
    connect(m_nextButton, &QPushButton::clicked, this, &MainWindow::goNext);
    connect(m_table, &QTableView::doubleClicked, this, &MainWindow::openInBrowser);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &current, const QModelIndex &) { showDetails(current); });
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Export results as &CSV..."), this, &MainWindow::exportCsv);
    file->addAction(tr("Export &PMID list..."), this, &MainWindow::exportPmids);
    file->addSeparator();
    file->addAction(tr("&Settings..."), this, &MainWindow::editSettings);
    file->addSeparator();
    file->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("PubMed search help (website)"), this, []() {
        QDesktopServices::openUrl(QUrl(QStringLiteral("https://pubmed.ncbi.nlm.nih.gov/help/")));
    });
    help->addAction(tr("&About"), this, &MainWindow::about);
}

void MainWindow::loadSettings()
{
    QSettings s;
    m_client.setApiKey(s.value(QStringLiteral("apiKey")).toString());
    m_client.setEmail(s.value(QStringLiteral("email")).toString());
}

// Builds the PubMed query from the search box plus the filter controls.
// The user's text is wrapped in parentheses so filters apply to the whole query.
QString MainWindow::buildTerm() const
{
    const QString text = m_query->text().trimmed();
    if (text.isEmpty())
        return {};

    QStringList parts{QLatin1Char('(') + text + QLatin1Char(')')};

    const QString date = m_dateCombo->currentData().toString();
    if (!date.isEmpty())
        parts << date;

    const QString type = m_typeCombo->currentData().toString();
    if (!type.isEmpty())
        parts << type;

    if (m_freeFullText->isChecked())
        parts << QStringLiteral("free full text[sb]");
    if (m_hasAbstract->isChecked())
        parts << QStringLiteral("hasabstract");

    return parts.join(QStringLiteral(" AND "));
}

void MainWindow::startSearch()
{
    const QString term = buildTerm();
    if (term.isEmpty()) {
        statusBar()->showMessage(tr("Type something to search for."), 3000);
        return;
    }
    m_term = term;
    m_sort = m_sortCombo->currentData().toString();
    m_start = 0;
    runSearch();
}

void MainWindow::goPrevious()
{
    m_start = qMax(0, m_start - kPageSize);
    runSearch();
}

void MainWindow::goNext()
{
    m_start += kPageSize;
    runSearch();
}

void MainWindow::runSearch()
{
    m_client.search(m_term, m_sort, m_start, kPageSize);
}

void MainWindow::onSearchFinished(int total, const QStringList &pmids, const QString &translatedQuery)
{
    m_total = total;
    m_info->setToolTip(tr("PubMed interpreted your search as:\n%1").arg(translatedQuery));

    if (total == 0 || pmids.isEmpty()) {
        m_info->setText(tr("No results found."));
        return;
    }

    const QLocale loc;
    QString text = tr("Results %1-%2 of %3")
                       .arg(loc.toString(m_start + 1))
                       .arg(loc.toString(m_start + static_cast<int>(pmids.size())))
                       .arg(loc.toString(total));
    if (total > kMaxResults)
        text += tr("  (PubMed shows at most %1 - narrow your search)").arg(loc.toString(kMaxResults));
    m_info->setText(text);
}

void MainWindow::onArticlesReady(const QList<Article> &articles)
{
    m_articles = articles;

    m_model->removeRows(0, m_model->rowCount());
    for (const Article &a : articles) {
        m_model->appendRow({new QStandardItem(a.year),
                            new QStandardItem(a.title),
                            new QStandardItem(shortAuthors(a.authors)),
                            new QStandardItem(a.journal),
                            new QStandardItem(a.pmid)});
        for (int c = 0; c < m_model->columnCount(); ++c)
            m_model->item(m_model->rowCount() - 1, c)->setToolTip(a.title);
    }

    m_detail->clear();
    if (!articles.isEmpty())
        m_table->selectRow(0);

    statusBar()->showMessage(tr("%n result(s) loaded.", nullptr, static_cast<int>(articles.size())), 4000);
    updatePaging();
}

void MainWindow::onFailed(const QString &message)
{
    statusBar()->showMessage(tr("Search failed."), 4000);
    QMessageBox::warning(this, tr("PubMed Search"), message);
}

void MainWindow::onBusyChanged(bool busy)
{
    m_busy = busy;
    if (busy)
        statusBar()->showMessage(tr("Searching PubMed..."));
    updatePaging();
}

void MainWindow::updatePaging()
{
    const bool hasMore = (m_start + kPageSize < m_total) && (m_start + kPageSize < kMaxResults);
    m_prevButton->setEnabled(!m_busy && m_start > 0);
    m_nextButton->setEnabled(!m_busy && hasMore);
}

void MainWindow::showDetails(const QModelIndex &current)
{
    const int row = current.row();
    if (row < 0 || row >= m_articles.size()) {
        m_detail->clear();
        return;
    }
    const Article &a = m_articles.at(row);

    QString html = QStringLiteral("<h3>%1</h3>").arg(a.title.toHtmlEscaped());
    if (!a.authors.isEmpty())
        html += QStringLiteral("<p>%1</p>").arg(a.authors.join(QStringLiteral(", ")).toHtmlEscaped());
    html += QStringLiteral("<p><i>%1</i> %2 &middot; PMID %3</p>")
                .arg(a.journal.toHtmlEscaped(), a.year.toHtmlEscaped(), a.pmid.toHtmlEscaped());

    html += QStringLiteral("<p><a href=\"%1\">Open in PubMed</a>").arg(pubmedUrl(a.pmid));
    if (!a.doi.isEmpty())
        html += QStringLiteral(" &nbsp;|&nbsp; <a href=\"https://doi.org/%1\">DOI: %2</a>")
                    .arg(a.doi.toHtmlEscaped(), a.doi.toHtmlEscaped());
    html += QStringLiteral("</p>");

    if (a.abstractText.isEmpty()) {
        html += QStringLiteral("<p><i>%1</i></p>").arg(tr("No abstract available."));
    } else {
        for (const QString &para : a.abstractText.split(QStringLiteral("\n\n"), Qt::SkipEmptyParts))
            html += QStringLiteral("<p>%1</p>").arg(para.toHtmlEscaped());
    }
    m_detail->setHtml(html);
}

void MainWindow::openInBrowser(const QModelIndex &index)
{
    const int row = index.row();
    if (row >= 0 && row < m_articles.size())
        QDesktopServices::openUrl(QUrl(pubmedUrl(m_articles.at(row).pmid)));
}

void MainWindow::exportCsv()
{
    if (m_articles.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("There are no results to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export results as CSV"),
                                                      QStringLiteral("pubmed_results.csv"),
                                                      tr("CSV files (*.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export"), tr("Could not write to %1").arg(path));
        return;
    }
    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out.setGenerateByteOrderMark(true); // lets Excel detect UTF-8

    out << "PMID,Year,Title,Authors,Journal,DOI,Abstract\n";
    for (const Article &a : m_articles) {
        out << csvField(a.pmid) << ',' << csvField(a.year) << ',' << csvField(a.title) << ','
            << csvField(a.authors.join(QStringLiteral("; "))) << ',' << csvField(a.journal) << ','
            << csvField(a.doi) << ',' << csvField(a.abstractText) << '\n';
    }
    statusBar()->showMessage(tr("Exported %1 results (current page).").arg(m_articles.size()), 5000);
}

void MainWindow::exportPmids()
{
    if (m_articles.isEmpty()) {
        QMessageBox::information(this, tr("Export"), tr("There are no results to export."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export PMID list"),
                                                      QStringLiteral("pmids.txt"),
                                                      tr("Text files (*.txt)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export"), tr("Could not write to %1").arg(path));
        return;
    }
    QTextStream out(&file);
    for (const Article &a : m_articles)
        out << a.pmid << '\n';
    statusBar()->showMessage(tr("Exported %1 PMIDs (current page).").arg(m_articles.size()), 5000);
}

void MainWindow::editSettings()
{
    QSettings s;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Settings"));
    auto *form = new QFormLayout(&dlg);

    auto *email = new QLineEdit(s.value(QStringLiteral("email")).toString());
    email->setPlaceholderText(tr("optional - NCBI can contact you if there is a problem"));
    auto *key = new QLineEdit(s.value(QStringLiteral("apiKey")).toString());
    key->setEchoMode(QLineEdit::Password);
    key->setPlaceholderText(tr("optional - raises the limit from 3 to 10 requests/second"));

    form->addRow(tr("Email:"), email);
    form->addRow(tr("NCBI API key:"), key);
    form->addRow(new QLabel(tr("Neither is required for normal use.")));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    dlg.setMinimumWidth(480);

    if (dlg.exec() == QDialog::Accepted) {
        s.setValue(QStringLiteral("email"), email->text().trimmed());
        s.setValue(QStringLiteral("apiKey"), key->text().trimmed());
        loadSettings();
    }
}

void MainWindow::about()
{
    QMessageBox::about(
        this, tr("About PubMed Search"),
        tr("<b>PubMed Search</b> 0.1<br><br>"
           "A simple desktop search tool built on the NCBI E-utilities.<br><br>"
           "Records come from PubMed, a service of the U.S. National Library of Medicine. "
           "This application is not affiliated with or endorsed by NCBI or NLM. "
           "Please see the <a href=\"https://www.ncbi.nlm.nih.gov/About/disclaimer.html\">"
           "NCBI Disclaimer and Copyright notice</a>; full-text articles opened from PubMed "
           "are subject to their providers' copyright terms."));
}
