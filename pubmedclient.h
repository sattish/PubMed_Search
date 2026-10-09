#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <functional>

#include "article.h"

// Talks to NCBI E-utilities (ESearch + EFetch).
// Requests are sent one at a time and spaced out so the NCBI limit
// (3 requests/s without an API key, 10/s with one) is never exceeded.
class PubMedClient : public QObject {
    Q_OBJECT
public:
    explicit PubMedClient(QObject *parent = nullptr);

    void setApiKey(const QString &key) { m_apiKey = key.trimmed(); }
    void setEmail(const QString &email) { m_email = email.trimmed(); }

    // sort: "relevance", "pub_date", "Author" or "JournalName"
    void search(const QString &term, const QString &sort, int start, int count);
    void cancel();

signals:
    void searchFinished(int totalCount, const QStringList &pmids, const QString &translatedQuery);
    void articlesReady(const QList<Article> &articles);
    void failed(const QString &message);
    void busyChanged(bool busy);

private:
    struct Request {
        QUrl url;
        std::function<void(const QByteArray &)> onSuccess;
        quint64 generation;
    };

    void enqueue(const QString &url, std::function<void(const QByteArray &)> onSuccess);
    void pump();
    void onReplyFinished(quint64 generation, const std::function<void(const QByteArray &)> &onSuccess);
    void handleSearchReply(const QByteArray &data);
    void fetchArticles(const QStringList &pmids);
    void setBusy(bool busy);
    QString commonParams() const;

    QNetworkAccessManager m_nam;
    QQueue<Request> m_queue;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timer;
    QElapsedTimer m_sinceLast;
    quint64 m_generation = 0;
    bool m_busy = false;
    QString m_apiKey;
    QString m_email;
};
