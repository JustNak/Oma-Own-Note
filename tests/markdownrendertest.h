#pragma once

#include <QObject>

class MarkdownRenderTest : public QObject {
    Q_OBJECT

private slots:
    void sanitizerDropsActiveContent();
    void sanitizerNeutralizesUrlsAndHandlers();
    void sanitizerKeepsSafeMarkup();
    void rendersCommonMark();
    void rendersGfm();
    void highlightsFencedCode();
    void neutralizesPayloadsInTheDocument();
    void previewRendersTheSpecimen();
};
