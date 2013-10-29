/****************************************************************************
**
** Copyright (C) 2013 Digia Plc and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/legal
**
** This file is part of the QtWebEngine module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Digia.  For licensing terms and
** conditions see http://qt.digia.com/licensing.  For further information
** use the contact form at http://qt.digia.com/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Digia gives you certain additional
** rights.  These rights are described in the Digia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 3.0 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU General Public License version 3.0 requirements will be
** met: http://www.gnu.org/copyleft/gpl.html.
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "delegated_frame_node.h"

#if (QT_VERSION >= QT_VERSION_CHECK(5, 2, 0))

// On Mac we need to reset this define in order to prevent definition
// of "check" macros etc. The "check" macro collides with a member function name in QtQuick.
// See AssertMacros.h in the Mac SDK.
#include <QtGlobal> // We need this for the Q_OS_MAC define.
#if defined(Q_OS_MAC)
#undef __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES
#define __ASSERT_MACROS_DEFINE_VERSIONS_WITHOUT_UNDERSCORES 0
#endif

#include "chromium_gpu_helper.h"
#include "type_conversion.h"

#include "base/message_loop/message_loop.h"
#include "base/bind.h"
#include "cc/output/delegated_frame_data.h"
#include "cc/quads/draw_quad.h"
#include "cc/quads/render_pass_draw_quad.h"
#include "cc/quads/texture_draw_quad.h"
#include "cc/quads/tile_draw_quad.h"
#include <QOpenGLFramebufferObject>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtQuick/private/qquickclipnode_p.h>
#include <QtQuick/private/qquickwindow_p.h>
#include <QtQuick/private/qsgcontext_p.h>
#include <QtQuick/private/qsgrenderer_p.h>
#include <QtQuick/private/qsgtexture_p.h>

class RenderPassTexture : public QSGTexture
{
public:
    RenderPassTexture(const cc::RenderPass::Id &id, QSGRenderContext *context);

    const cc::RenderPass::Id &id() const { return m_id; }
    void bind();

    int textureId() const { return m_fbo ? m_fbo->texture() : 0; }
    QSize textureSize() const { return m_rect.size(); }
    bool hasAlphaChannel() const { return m_format != GL_RGB; }
    bool hasMipmaps() const { return false; }

    void setRect(const QRect &rect) { m_rect = rect; }
    void setFormat(GLenum format) { m_format = format; }
    void setDevicePixelRatio(qreal ratio) { m_device_pixel_ratio = ratio; }
    QSGNode *rootNode() { return m_rootNode.data(); }

    void grab();

private:
    cc::RenderPass::Id m_id;
    QRect m_rect;
    qreal m_device_pixel_ratio;
    GLenum m_format;

    QScopedPointer<QSGRootNode> m_rootNode;
    QScopedPointer<QSGRenderer> m_renderer;
    QScopedPointer<QOpenGLFramebufferObject> m_fbo;

    QSGRenderContext *m_context;
};

class MailboxTexture : public QSGTexture {
public:
    MailboxTexture(const cc::TransferableResource *resource, bool hasAlpha);
    virtual int textureId() const { return m_textureId; }
    void setTextureSize(const QSize& size) { m_textureSize = size; }
    virtual QSize textureSize() const { return m_textureSize; }
    virtual bool hasAlphaChannel() const { return m_hasAlpha; }
    virtual bool hasMipmaps() const { return false; }
    virtual void bind();

    bool needsToFetch() const { return m_resource; }
    const cc::TransferableResource *resource() const { return m_resource; }
    void fetchTexture(gpu::gles2::MailboxManager *mailboxManager);

private:
    // This is just a pointer to the cc::DelegatedFrameData. It has to be held until DelegatedFrameNode::preprocess is done.
    const cc::TransferableResource *m_resource;
    int m_textureId;
    QSize m_textureSize;
    bool m_hasAlpha;
};

static const cc::TransferableResource *findResource(const cc::TransferableResourceArray &array, unsigned id)
{
    // A 0 ID means that there is no resource.
    if (!id)
        return 0;

    for (unsigned i = 0; i < array.size(); ++i) {
        const cc::TransferableResource &res = array.at(i);
        if (res.id == id)
            return &res;
    }
    return 0;
}

static inline QSharedPointer<RenderPassTexture> findRenderPassTexture(const cc::RenderPass::Id &id, const QList<QSharedPointer<RenderPassTexture> > &list)
{
    Q_FOREACH (const QSharedPointer<RenderPassTexture> &texture, list)
        if (texture->id() == id)
            return texture;
    return QSharedPointer<RenderPassTexture>();
}

static QSGNode *buildRenderPassChain(QSGNode *chainParent, const cc::RenderPass *renderPass)
{
    // Chromium already ordered the quads from back to front for us, however the
    // Qt scene graph layers individual geometries in their own z-range and uses
    // the depth buffer to visually stack nodes according to their item tree order.
    // This finds the z-span of all layers so that we can z-compress them to fit
    // them between 0.0 and 1.0 on the z axis.
    double minZ = 0;
    double maxZ = 1;
    double src2[8];
    double dst4[16];
    // topleft.x, topleft.y, topRight.y and bottomLeft.x
    src2[0] = src2[1] = src2[3] = src2[4] = 0;

    // Go through each layer in this pass and find out their transformed rect.
    cc::SharedQuadStateList::const_iterator it = renderPass->shared_quad_state_list.begin();
    cc::SharedQuadStateList::const_iterator sharedStateEnd = renderPass->shared_quad_state_list.end();
    for (; it != sharedStateEnd; ++it) {
        gfx::Size &layerSize = (*it)->content_bounds;
        // topRight.x
        src2[2] = layerSize.width();
        // bottomLeft.y
        src2[5] = layerSize.height();
        // bottomRight
        src2[6] = layerSize.width();
        src2[7] = layerSize.height();
        (*it)->content_to_target_transform.matrix().map2(src2, 4, dst4);
        // Check the mapped corner's z value and track the boundaries.
        minZ = std::min(std::min(std::min(std::min(minZ, dst4[2]), dst4[6]), dst4[10]), dst4[14]);
        maxZ = std::max(std::max(std::max(std::max(maxZ, dst4[2]), dst4[6]), dst4[10]), dst4[14]);
    }

    QSGTransformNode *zCompressNode = new QSGTransformNode;
    QMatrix4x4 zCompressMatrix;
    zCompressMatrix.scale(1, 1, 1 / (maxZ - minZ));
    zCompressMatrix.translate(0, 0, -minZ);
    zCompressNode->setMatrix(zCompressMatrix);
    chainParent->appendChildNode(zCompressNode);
    return zCompressNode;
}

static QSGNode *buildLayerChain(QSGNode *chainParent, const cc::SharedQuadState *layerState)
{
    QSGNode *layerChain = chainParent;
    if (layerState->is_clipped) {
        QQuickDefaultClipNode *clipNode = new QQuickDefaultClipNode(toQt(layerState->clip_rect));
        clipNode->update();
        layerChain->appendChildNode(clipNode);
        layerChain = clipNode;
    }
    if (!layerState->content_to_target_transform.IsIdentity()) {
        QSGTransformNode *transformNode = new QSGTransformNode;
        transformNode->setMatrix(toQt(layerState->content_to_target_transform.matrix()));
        layerChain->appendChildNode(transformNode);
        layerChain = transformNode;
    }
    if (layerState->opacity < 1.0) {
        QSGOpacityNode *opacityNode = new QSGOpacityNode;
        opacityNode->setOpacity(layerState->opacity);
        layerChain->appendChildNode(opacityNode);
        layerChain = opacityNode;
    }
    return layerChain;
}

RenderPassTexture::RenderPassTexture(const cc::RenderPass::Id &id, QSGRenderContext *context)
    : QSGTexture()
    , m_id(id)
    , m_device_pixel_ratio(1)
    , m_format(GL_RGBA)
    , m_rootNode(new QSGRootNode)
    , m_context(context)
{
}

void RenderPassTexture::bind()
{
    glBindTexture(GL_TEXTURE_2D, m_fbo ? m_fbo->texture() : 0);
    updateBindOptions();
}

void RenderPassTexture::grab()
{
    if (!m_rootNode->firstChild()) {
        m_renderer.reset();
        m_fbo.reset();
        return;
    }
    if (!m_renderer) {
        m_renderer.reset(m_context->createRenderer());
        m_renderer->setRootNode(m_rootNode.data());
    }
    m_renderer->setDevicePixelRatio(m_device_pixel_ratio);

    if (!m_fbo || m_fbo->size() != m_rect.size() || m_fbo->format().internalTextureFormat() != m_format)
    {
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setInternalTextureFormat(m_format);

        m_fbo.reset(new QOpenGLFramebufferObject(m_rect.size(), format));
        glBindTexture(GL_TEXTURE_2D, m_fbo->texture());
        updateBindOptions(true);
    }

    m_rootNode->markDirty(QSGNode::DirtyForceUpdate); // Force matrix, clip and opacity update.
    m_renderer->nodeChanged(m_rootNode.data(), QSGNode::DirtyForceUpdate); // Force render list update.

    m_renderer->setDeviceRect(m_rect.size());
    m_renderer->setViewportRect(m_rect.size());
    QRectF mirrored(m_rect.left(), m_rect.bottom(), m_rect.width(), -m_rect.height());
    m_renderer->setProjectionMatrixToRect(mirrored);
    m_renderer->setClearColor(Qt::transparent);

    m_context->renderNextFrame(m_renderer.data(), m_fbo->handle());
}

MailboxTexture::MailboxTexture(const cc::TransferableResource *resource, bool hasAlpha)
    : m_resource(resource)
    , m_textureId(0)
    , m_textureSize(toQt(resource->size))
    , m_hasAlpha(hasAlpha)
{
}

void MailboxTexture::bind()
{
    glBindTexture(GL_TEXTURE_2D, m_textureId);
}

void MailboxTexture::fetchTexture(gpu::gles2::MailboxManager *mailboxManager)
{
    Q_ASSERT(m_resource);
    gpu::gles2::Texture *tex = ConsumeTexture(mailboxManager, GL_TEXTURE_2D, *reinterpret_cast<const gpu::gles2::MailboxName*>(m_resource->mailbox.name));

    // The texture might already have been deleted.
    // FIXME: We might be able to avoid this check with better synchronization.
    if (tex)
        m_textureId = service_id(tex);

    // We do not need to fetch this texture again.
    m_resource = 0;
}

DelegatedFrameNode::DelegatedFrameNode(QQuickWindow *window)
    : m_window(window)
    , m_numPendingSyncPoints(0)
{
    setFlag(UsePreprocess);
}

DelegatedFrameNode::~DelegatedFrameNode()
{
}

void DelegatedFrameNode::preprocess()
{
    // With the threaded render loop the GUI thread has been unlocked at this point.
    // We can now wait for the Chromium GPU thread to produce textures that will be
    // rendered on our quads and fetch the IDs from the mailboxes we were given.
    QList<MailboxTexture *> mailboxesToFetch;
    Q_FOREACH (const QSharedPointer<MailboxTexture> &mailboxTexture, m_mailboxTextures.values())
        if (mailboxTexture->needsToFetch())
            mailboxesToFetch.append(mailboxTexture.data());

    if (!mailboxesToFetch.isEmpty()) {
        QMutexLocker lock(&m_mutex);
        base::MessageLoop *gpuMessageLoop = gpu_message_loop();
        content::SyncPointManager *syncPointManager = sync_point_manager();

        Q_FOREACH (MailboxTexture *mailboxTexture, mailboxesToFetch) {
            m_numPendingSyncPoints++;
            AddSyncPointCallbackOnGpuThread(gpuMessageLoop, syncPointManager, mailboxTexture->resource()->sync_point, base::Bind(&DelegatedFrameNode::syncPointRetired, this, &mailboxesToFetch));
        }

        m_mailboxesFetchedWaitCond.wait(&m_mutex);
    }

    // Then render any intermediate RenderPass in order.
    Q_FOREACH (const QSharedPointer<RenderPassTexture> &renderPass, m_renderPassTextures)
        renderPass->grab();
}

void DelegatedFrameNode::commit(cc::DelegatedFrameData *frameData)
{
    // Keep the old texture lists around to find the ones we can re-use.
    QList<QSharedPointer<RenderPassTexture> > oldRenderPassTextures;
    m_renderPassTextures.swap(oldRenderPassTextures);
    QMap<int, QSharedPointer<MailboxTexture> > oldMailboxTextures;
    m_mailboxTextures.swap(oldMailboxTextures);

    // The RenderPasses list is actually a tree where a parent RenderPass is connected
    // to its dependencies through a RenderPass::Id reference in one or more RenderPassQuads.
    // The list is already ordered with intermediate RenderPasses placed before their
    // parent, with the last one in the list being the root RenderPass, the one
    // that we displayed to the user.
    // All RenderPasses except the last one are rendered to an FBO.
    cc::RenderPass *rootRenderPass = frameData->render_pass_list.back();

    for (unsigned i = 0; i < frameData->render_pass_list.size(); ++i) {
        cc::RenderPass *pass = frameData->render_pass_list.at(i);

        QSGNode *renderPassParent = 0;
        if (pass != rootRenderPass) {
            QSharedPointer<RenderPassTexture> rpTexture = findRenderPassTexture(pass->id, oldRenderPassTextures);
            if (!rpTexture) {
                QSGRenderContext *sgrc = QQuickWindowPrivate::get(m_window)->context;
                rpTexture = QSharedPointer<RenderPassTexture>(new RenderPassTexture(pass->id, sgrc));
            }
            m_renderPassTextures.append(rpTexture);
            rpTexture->setDevicePixelRatio(m_window->devicePixelRatio());
            rpTexture->setRect(toQt(pass->output_rect));
            rpTexture->setFormat(pass->has_transparent_background ? GL_RGBA : GL_RGB);
            renderPassParent = rpTexture->rootNode();
        } else
            renderPassParent = this;

        // There is currently no way to know which and how quads changed since the last frame.
        // We have to reconstruct the node chain with their geometries on every update.
        while (QSGNode *oldChain = renderPassParent->firstChild())
            delete oldChain;

        QSGNode *renderPassChain = buildRenderPassChain(renderPassParent, pass);
        const cc::SharedQuadState *currentLayerState = 0;
        QSGNode *currentLayerChain = 0;

        cc::QuadList::ConstBackToFrontIterator it = pass->quad_list.BackToFrontBegin();
        cc::QuadList::ConstBackToFrontIterator end = pass->quad_list.BackToFrontEnd();
        for (; it != end; ++it) {
            cc::DrawQuad *quad = *it;

            if (currentLayerState != quad->shared_quad_state) {
                currentLayerState = quad->shared_quad_state;
                currentLayerChain = buildLayerChain(renderPassChain, currentLayerState);
            }

            switch (quad->material) {
            case cc::DrawQuad::RENDER_PASS: {
                const cc::RenderPassDrawQuad *renderPassQuad = cc::RenderPassDrawQuad::MaterialCast(quad);
                QSGTexture *texture = findRenderPassTexture(renderPassQuad->render_pass_id, m_renderPassTextures).data();
                if (texture) {
                    QSGSimpleTextureNode *textureNode = new QSGSimpleTextureNode;
                    textureNode->setRect(toQt(quad->rect));
                    textureNode->setTexture(texture);
                    currentLayerChain->appendChildNode(textureNode);
                } else {
                    qWarning("Unknown RenderPass layer: Id %d", renderPassQuad->render_pass_id.layer_id);
                    continue;
                }
                break;
            } case cc::DrawQuad::TEXTURE_CONTENT: {
                const cc::TextureDrawQuad *tquad = cc::TextureDrawQuad::MaterialCast(quad);
                const cc::TransferableResource *res = findResource(frameData->resource_list, tquad->resource_id);

                // See if we already have a texture for this resource ID. The ID changes when the contents is updated,
                // even if the GL texture ID is the same as a previous resource.
                // Reusing a texture only saves us the sync point waiting and mailbox fetching.
                QSharedPointer<MailboxTexture> &texture = m_mailboxTextures[res->id] = oldMailboxTextures.value(res->id);
                if (!texture) {
                    texture = QSharedPointer<MailboxTexture>(new MailboxTexture(res, quad->ShouldDrawWithBlending()));
                    // TransferableResource::size isn't always set properly for TextureDrawQuads, use the size of its DrawQuad::rect instead.
                    texture->setTextureSize(toQt(quad->rect.size()));
                }

                QSGSimpleTextureNode *textureNode = new QSGSimpleTextureNode;
                textureNode->setTextureCoordinatesTransform(tquad->flipped ? QSGSimpleTextureNode::MirrorVertically : QSGSimpleTextureNode::NoTransform);
                textureNode->setRect(toQt(quad->rect));
                textureNode->setFiltering(res->filter == GL_LINEAR ? QSGTexture::Linear : QSGTexture::Nearest);
                textureNode->setTexture(texture.data());
                currentLayerChain->appendChildNode(textureNode);
                break;
            } case cc::DrawQuad::TILED_CONTENT: {
                const cc::TileDrawQuad *tquad = cc::TileDrawQuad::MaterialCast(quad);
                const cc::TransferableResource *res = findResource(frameData->resource_list, tquad->resource_id);

                QSharedPointer<MailboxTexture> &texture = m_mailboxTextures[res->id] = oldMailboxTextures.value(res->id);
                if (!texture)
                    texture = QSharedPointer<MailboxTexture>(new MailboxTexture(res, quad->ShouldDrawWithBlending()));

                QSGSimpleTextureNode *textureNode = new QSGSimpleTextureNode;
                textureNode->setRect(toQt(quad->rect));
                textureNode->setFiltering(res->filter == GL_LINEAR ? QSGTexture::Linear : QSGTexture::Nearest);
                textureNode->setTexture(texture.data());

                // FIXME: Find out if we can implement a QSGSimpleTextureNode::setSourceRect instead of this hack.
                // This has to be done at the end since many QSGSimpleTextureNode methods would overwrite this.
                QSGGeometry::updateTexturedRectGeometry(textureNode->geometry(), textureNode->rect(), textureNode->texture()->convertToNormalizedSourceRect(toQt(tquad->tex_coord_rect)));
                currentLayerChain->appendChildNode(textureNode);

                break;
            } default:
                qWarning("Unimplemented quad material: %d", quad->material);
                continue;
            }
        }
    }
}

void DelegatedFrameNode::fetchTexturesAndUnlockQt(DelegatedFrameNode *frameNode, QList<MailboxTexture *> *mailboxesToFetch)
{
    // Fetch texture IDs from the mailboxes while we're on the GPU thread, where the MailboxManager lives.
    gpu::gles2::MailboxManager *mailboxManager = mailbox_manager();
    Q_FOREACH (MailboxTexture *mailboxTexture, *mailboxesToFetch)
        mailboxTexture->fetchTexture(mailboxManager);

    // glFlush before yielding to the SG thread, whose context might already start using
    // some shared resources provided by the unflushed context here, on the Chromium GPU thread.
    glFlush();

    // Chromium provided everything we were waiting for, let Qt start rendering.
    QMutexLocker lock(&frameNode->m_mutex);
    frameNode->m_mailboxesFetchedWaitCond.wakeOne();
}

void DelegatedFrameNode::syncPointRetired(DelegatedFrameNode *frameNode, QList<MailboxTexture *> *mailboxesToFetch)
{
    // The way that sync points are normally used by the GpuCommandBufferStub is that it asks
    // the GpuScheduler to resume the work of the associated GL command stream / context once
    // the sync point has been retired by the dependency's context. In other words, a produced
    // texture means that the mailbox can be consumed, but the texture itself isn't expected
    // to be ready until to control is given back to the GpuScheduler through the event loop.
    // Do the same for our implementation by posting a message to the event loop once the last
    // of our syncpoints has been retired (the syncpoint callback is called synchronously) and
    // only at this point we wake the Qt rendering thread.
    QMutexLocker lock(&frameNode->m_mutex);
    if (!--frameNode->m_numPendingSyncPoints)
        base::MessageLoop::current()->PostTask(FROM_HERE, base::Bind(&DelegatedFrameNode::fetchTexturesAndUnlockQt, frameNode, mailboxesToFetch));
}

#endif // QT_VERSION