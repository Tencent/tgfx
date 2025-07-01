#ifndef TEXTURELISTDRAWER_H
#define TEXTURELISTDRAWER_H

#include <QStringList>
#include <QVector>
#include <memory>
#include "tgfx/gpu/opengl/qt/QGLWindow.h"
#include "AppHost.h"
namespace inspector {
    class TextureListDrawer : public QQuickItem
    {
        Q_OBJECT
        Q_PROPERTY(QString imageLabel  WRITE setImageLabel)
    public:
        explicit TextureListDrawer(QQuickItem *parent = nullptr);
        ~TextureListDrawer() = default;

        Q_SIGNAL void selectedImage(std::shared_ptr<tgfx::Image> image);

        void updateImageData(QStringList testImageSources);
        void setImageLabel(const QString &label);

    protected:
        QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
        void mousePressEvent(QMouseEvent *event) override;
        void wheelEvent(QWheelEvent *event) override;
        void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)override;

    private:
        void updateLayout();
        void drawContent(tgfx::Canvas* canvas);
        int itemAtPosition(qreal y) const;

        QVector<tgfx::Rect> m_squareRects;
        QVector<std::shared_ptr<tgfx::Image>> m_images;
        bool m_layoutDirty = true;
        qreal m_scrollOffset;

        std::shared_ptr<tgfx::QGLWindow> m_tgfxWindow = nullptr;
        std::shared_ptr<AppHost> m_appHost = nullptr;
    };
}

#endif // TEXTURELISTDRAWER_H