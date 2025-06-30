#include "TextureListDrawer.h"
#include <QRandomGenerator>
#include <iostream>
#include <qdir.h>

#include "Draw.h"
#include <QSGImageNode>
namespace inspector {
    TextureListDrawer::TextureListDrawer(QQuickItem *parent)
        : QQuickItem(parent), m_appHost(AppHostSingleton::GetInstance())
    {
        setFlag(ItemHasContents, true);
        setAcceptHoverEvents(true);
        setAcceptedMouseButtons(Qt::AllButtons);
        updateImageData();
    }

    void TextureListDrawer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
    {
        QQuickItem::geometryChange(newGeometry, oldGeometry);
        m_layoutDirty = true;
        update();
    }

    void TextureListDrawer::updateLayout()
    {
        if (!m_layoutDirty) return;

        m_squareRects.clear();
        float y = 0;
        const float squareSize = static_cast<float>(width());
        for (int i=0;i<m_images.size();i++) {
            m_squareRects.append(tgfx::Rect::MakeXYWH(0.f, y, squareSize, squareSize));
            y += squareSize;
        }

        setImplicitHeight(y);
        m_layoutDirty = false;
    }

    int TextureListDrawer::itemAtPosition(qreal y) const
    {
        for (int i = 0; i < m_squareRects.size(); ++i) {
            if (m_squareRects[i].contains(0.f, static_cast<float>(y))) {
                return i;
            }
        }
        return -1;
    }

    void TextureListDrawer::mousePressEvent(QMouseEvent *event)
    {
        QQuickItem::mousePressEvent(event);

        int index = itemAtPosition(event->position().y()+ m_scrollOffset*width()/200.f);
        if (index >= 0 && index < m_images.size()) {
            emit selectedImage(m_images[index]);
        }
    }

    void TextureListDrawer::wheelEvent(QWheelEvent *event)
    {
        const qreal maxScroll = implicitHeight() - height();
        if (maxScroll <= 0) {
            m_scrollOffset = 0;
            return;
        }
        const qreal delta = event->angleDelta().y() / 120.0;
        const qreal step = 20.0;

        m_scrollOffset -= delta * step;
        m_scrollOffset = qBound(0.0, m_scrollOffset, maxScroll/width()*200.f);

        update();
        event->accept();
    }

    void TextureListDrawer::drawContent(tgfx::Canvas* canvas)
    {
        if (!canvas) return;
        canvas->clear();
        canvas->setMatrix(tgfx::Matrix::MakeScale(m_appHost->density(), m_appHost->density()));
        canvas->translate(0, -static_cast<float>(m_scrollOffset*width())/200.f);
        for (int i = 0; i < m_squareRects.size(); ++i) {
            drawRect(canvas,m_squareRects[i], 0xFF2E2E2E);
            // 绘制内部图片
            if (!m_images[i])return;
            canvas->drawImageRect(m_images[i],m_squareRects[i],tgfx::SamplingOptions(tgfx::FilterMode::Linear, tgfx::MipmapMode::Linear));
        }
    }

    void TextureListDrawer::updateImageData() {

        QString rootPath = QCoreApplication::applicationDirPath();
        // 获取项目根目录（假设resources在项目根目录下）
        while (!rootPath.isEmpty() && !QDir(rootPath + "/resources").exists()) {
            rootPath = rootPath.left(rootPath.lastIndexOf('/'));
        }
        QStringList testImageSources = {
                rootPath + "/resources/assets/bridge.jpg",
                rootPath + "/resources/assets/glyph1.png",
                rootPath + "/resources/assets/glyph2.png",
                rootPath + "/resources/assets/glyph3.png",
                rootPath + "/resources/apitest/rotation.jpg",
                rootPath + "/resources/apitest/rgbaaa.png"};

        m_images.clear();
        for (int i = 0; i < testImageSources.size(); ++i) {
            auto Image = tgfx::Image::MakeFromFile(testImageSources[i].toStdString());
            Image = Image->makeMipmapped(true);
            m_images.push_back(Image);
        }

        m_layoutDirty = true;
        update();
        emit selectedImage(nullptr);
    }

    QSGNode *TextureListDrawer::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
    {
        updateLayout();

        if (!m_tgfxWindow)
            m_tgfxWindow = tgfx::QGLWindow::MakeFrom(this, true);

        auto pixelRatio = window() ? window()->devicePixelRatio() : 1.0;
        auto screenWidth = static_cast<int>(ceil(width() * pixelRatio));
        auto screenHeight = static_cast<int>(ceil(height() * pixelRatio));

        bool sizeChanged = m_appHost->updateScreen(screenWidth, screenHeight, static_cast<float>(pixelRatio));
        if (sizeChanged) {
            m_tgfxWindow->invalidSize();
        }

        // 绘制内容
        auto device = m_tgfxWindow->getDevice();
        if (device) {
            auto context = device->lockContext();
            if (context) {
                auto surface = m_tgfxWindow->getSurface(context);
                if (surface) {
                    auto canvas = surface->getCanvas();
                    drawContent(canvas);
                    context->flushAndSubmit();
                    m_tgfxWindow->present(context);
                }
                device->unlock();
            }
        }

        // 更新场景图节点
        auto node = static_cast<QSGImageNode*>(oldNode);
        auto texture = m_tgfxWindow->getQSGTexture();
        if (texture) {
            if (!node) {
                node = window()->createImageNode();
                node->setFiltering(QSGTexture::Linear);
            }
            node->setTexture(texture);
            node->setRect(boundingRect());
            node->markDirty(QSGNode::DirtyMaterial | QSGNode::DirtyGeometry);
        }

        return node;
    }
}
