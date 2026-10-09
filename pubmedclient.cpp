#include "pubmedclient.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QXmlStreamReader>

namespace {

const QString kBase = QStringLiteral("https://eutils.ncbi.nlm.nih.gov/entrez/eutils/");
const QString kTool = QStringLiteral("PubMedSearchDesktop");

QString enc(const QString &s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s));
}

// Reads one <PubmedArticle> element. The reader must be positioned on its start tag.
Article readArticle(QXmlStreamReader &r)
{
    static const QString pPmid = QStringLiteral("PubmedArticle/MedlineCitation/PMID");
    static const QString pTitle = QStringLiteral("PubmedArticle/MedlineCitation/Article/ArticleTitle");
    static const QString pJournal = QStringLiteral("PubmedArticle/MedlineCitation/Article/Journal/Title");
    static const QString pYear = QStringLiteral("PubmedArticle/MedlineCitation/Article/Journal/JournalIssue/PubDate/Year");
    static const QString pMedDate = QStringLiteral("PubmedArticle/MedlineCitation/Article/Journal/JournalIssue/PubDate/MedlineDate");
    static const QString pAbstract = QStringLiteral("PubmedArticle/MedlineCitation/Article/Abstract/AbstractText");
    static const QString pAuthor = QStringLiteral("PubmedArticle/MedlineCitation/Article/AuthorList/Author");
    static const QString pLast = pAuthor + QStringLiteral("/LastName");
    static const QString pInitials = pAuthor + QStringLiteral("/Initials");
    static const QString pCollective = pAuthor + QStringLiteral("/CollectiveName");
    static const QString pArticleId = QStringLiteral("PubmedArticle/PubmedData/ArticleIdList/ArticleId");

    Article a;
    QStringList path{QStringLiteral("PubmedArticle")};
    QStringList abstractParts;
    QString lastName, initials, collective, medlineDate;

    // Reads the text of the current leaf element (child markup included)
    // and pops it from the path, since readElementText() consumes the end tag.
    auto leaf = [&]() {
        const QString text = r.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        path.removeLast();
        return text;
    };

    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            path.append(r.name().toString());
            const QString p = path.join(QLatin1Char('/'));

            if (p == pPmid) {
                const QString t = leaf();
                if (a.pmid.isEmpty())
                    a.pmid = t;
            } else if (p == pTitle) {
                a.title = leaf();
            } else if (p == pJournal) {
                a.journal = leaf();
            } else if (p == pYear) {
                a.year = leaf();
            } else if (p == pMedDate) {
                medlineDate = leaf();
            } else if (p == pAbstract) {
                const QString label = r.attributes().value(QStringLiteral("Label")).toString();
                const QString t = leaf();
                if (!t.isEmpty())
                    abstractParts << (label.isEmpty() ? t : label + QStringLiteral(": ") + t);
            } else if (p == pLast) {
                lastName = leaf();
            } else if (p == pInitials) {
                initials = leaf();
            } else if (p == pCollective) {
                collective = leaf();
            } else if (p == pArticleId) {
                const QString type = r.attributes().value(QStringLiteral("IdType")).toString();
                const QString t = leaf();
                if (type == QLatin1String("doi") && a.doi.isEmpty())
                    a.doi = t;
            }
        } else if (r.isEndElement()) {
            if (path.join(QLatin1Char('/')) == pAuthor) {
                QString name = collective;
                if (name.isEmpty() && !lastName.isEmpty())
                    name = initials.isEmpty() ? lastName : lastName + QLatin1Char(' ') + initials;
                if (!name.isEmpty())
                    a.authors << name;
                lastName.clear();
                initials.clear();
                collective.clear();
            }
            path.removeLast();
            if (path.isEmpty())
                break;
        }
    }

    if (a.year.isEmpty() && medlineDate.size() >= 4)
        a.year = medlineDate.left(4);
    a.abstractText = abstractParts.join(QStringLiteral("\n\n"));
    return a;
}

QList<Article> parseArticles(const QByteArray &xml)
{
    QList<Article> out;
    QXmlStreamReader r(xml);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("PubmedArticle"))
            out.append(readArticle(r));
    }
    return out;
}

} // namespace

PubMedClient::PubMedClient(QObject *parent)
    : QObject(parent)
{
    m_nam.setTransferTimeout(30000);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &PubMedClient::pump);
}

QString PubMedClient::commonParams() const
{
    QString p = QStringLiteral("&tool=") + kTool;
    if (!m_email.isEmpty())
        p += QStringLiteral("&email=") + enc(m_email);
    if (!m_apiKey.isEmpty())
        p += QStringLiteral("&api_key=") + enc(m_apiKey);
    return p;
}

void PubMedClient::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void PubMedClient::cancel()
{
    ++m_generation;
    m_queue.clear();
    m_timer.stop();
    if (m_reply) {
        QNetworkReply *r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    setBusy(false);
}

void PubMedClient::search(const QString &term, const QString &sort, int start, int count)
{
    cancel();

    const QString url = kBase + QStringLiteral("esearch.fcgi?db=pubmed&retmode=json")
        + QStringLiteral("&term=") + enc(term)
        + QStringLiteral("&retstart=") + QString::number(start)
        + QStringLiteral("&retmax=") + QString::number(count)
        + QStringLiteral("&sort=") + enc(sort)
        + commonParams();

    enqueue(url, [this](const QByteArray &data) { handleSearchReply(data); });
}

void PubMedClient::fetchArticles(const QStringList &pmids)
{
    const QString url = kBase + QStringLiteral("efetch.fcgi?db=pubmed&retmode=xml&rettype=abstract")
        + QStringLiteral("&id=") + enc(pmids.join(QLatin1Char(',')))
        + commonParams();

    enqueue(url, [this, pmids](const QByteArray &data) {
        const QList<Article> parsed = parseArticles(data);
        if (parsed.isEmpty()) {
            emit failed(tr("PubMed returned no records. If this keeps happening you may be "
                           "hitting the rate limit - wait a moment and try again."));
            return;
        }
        // EFetch order is not guaranteed to match the ESearch order, so restore it.
        QHash<QString, Article> byId;
        for (const Article &a : parsed)
            byId.insert(a.pmid, a);
        QList<Article> ordered;
        for (const QString &id : pmids) {
            if (byId.contains(id))
                ordered.append(byId.value(id));
        }
        emit articlesReady(ordered);
    });
}

void PubMedClient::handleSearchReply(const QByteArray &data)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        emit failed(tr("Unexpected response from PubMed."));
        return;
    }
    const QJsonObject root = doc.object();
    if (root.contains(QStringLiteral("error"))) {
        emit failed(tr("PubMed error: %1").arg(root.value(QStringLiteral("error")).toString()));
        return;
    }
    const QJsonObject res = root.value(QStringLiteral("esearchresult")).toObject();
    if (res.contains(QStringLiteral("ERROR"))) {
        emit failed(tr("PubMed error: %1").arg(res.value(QStringLiteral("ERROR")).toString()));
        return;
    }

    const int total = res.value(QStringLiteral("count")).toString().toInt();
    QStringList ids;
    for (const QJsonValue &v : res.value(QStringLiteral("idlist")).toArray())
        ids << v.toString();
    const QString translated = res.value(QStringLiteral("querytranslation")).toString();

    emit searchFinished(total, ids, translated);

    if (ids.isEmpty())
        emit articlesReady({});
    else
        fetchArticles(ids);
}

void PubMedClient::enqueue(const QString &url, std::function<void(const QByteArray &)> onSuccess)
{
    m_queue.enqueue(Request{QUrl(url), std::move(onSuccess), m_generation});
    setBusy(true);
    pump();
}

void PubMedClient::pump()
{
    if (m_reply || m_queue.isEmpty() || m_timer.isActive())
        return;

    // NCBI allows 3 requests/s without a key and 10/s with one.
    const int interval = m_apiKey.isEmpty() ? 400 : 120;
    if (m_sinceLast.isValid()) {
        const qint64 elapsed = m_sinceLast.elapsed();
        if (elapsed < interval) {
            m_timer.start(static_cast<int>(interval - elapsed));
            return;
        }
    }

    const Request req = m_queue.dequeue();

    QNetworkRequest nr(req.url);
    nr.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PubMedSearchDesktop/0.1"));
    nr.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                    QNetworkRequest::NoLessSafeRedirectPolicy);

    m_sinceLast.restart();
    m_reply = m_nam.get(nr);

    const quint64 gen = req.generation;
    const auto cb = req.onSuccess;
    connect(m_reply, &QNetworkReply::finished, this, [this, gen, cb]() { onReplyFinished(gen, cb); });
}

void PubMedClient::onReplyFinished(quint64 generation,
                                   const std::function<void(const QByteArray &)> &onSuccess)
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    if (!reply)
        return;
    reply->deleteLater();

    if (generation != m_generation) { // result of a search that was replaced
        pump();
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString msg = reply->errorString();
        if (status == 429)
            msg = tr("Too many requests - PubMed's rate limit was exceeded. Wait a moment and try again.");
        m_queue.clear();
        setBusy(false);
        emit failed(msg);
        return;
    }

    onSuccess(reply->readAll());

    pump();
    if (!m_reply && m_queue.isEmpty() && !m_timer.isActive())
        setBusy(false);
}
