#pragma once

#include <QString>
#include <QStringList>

// One PubMed record, reduced to the fields the UI shows.
struct Article {
    QString pmid;
    QString title;
    QString journal;
    QString year;
    QString abstractText;
    QString doi;
    QStringList authors;
};
