#include "lc_global.h"
#include "lc_partselectionwidget.h"
#include "lc_application.h"
#include "lc_library.h"
#include "pieceinf.h"

static const int gIconSize = 64;

static int lcPartSortFunc(PieceInfo* const& a, PieceInfo* const& b)
{
	return strcmp(a->m_strDescription, b->m_strDescription);
}

lcPartSelectionFilterModel::lcPartSelectionFilterModel(QObject* Parent)
	: QSortFilterProxyModel(Parent)
{
}

void lcPartSelectionFilterModel::SetFilter(const QString& Filter)
{
	mFilter = Filter.toLatin1();
	invalidateFilter();
}

bool lcPartSelectionFilterModel::filterAcceptsRow(int SourceRow, const QModelIndex& SourceParent) const
{
	if (mFilter.isEmpty())
		return true;

	lcPartSelectionListModel* SourceModel = (lcPartSelectionListModel*)sourceModel();
	PieceInfo* Info = SourceModel->GetPieceInfo(SourceRow);

	return strstr(Info->m_strDescription, mFilter);
}

void lcPartSelectionItemDelegate::paint(QPainter* Painter, const QStyleOptionViewItem& Option, const QModelIndex& Index) const
{
	mListModel->RequestPreview(mFilterModel->mapToSource(Index).row());
	QStyledItemDelegate::paint(Painter, Option, Index);
}

QSize lcPartSelectionItemDelegate::sizeHint(const QStyleOptionViewItem& Option, const QModelIndex& Index) const
{
	return QStyledItemDelegate::sizeHint(Option, Index);
}

lcPartSelectionListModel::lcPartSelectionListModel(QObject* Parent)
	: QAbstractListModel(Parent)
{
}

lcPartSelectionListModel::~lcPartSelectionListModel()
{
}

void lcPartSelectionListModel::SetCategory(int CategoryIndex)
{
	beginResetModel();

	lcPiecesLibrary* Library = lcGetPiecesLibrary();
	lcArray<PieceInfo*> SingleParts, GroupedParts;

	Library->GetCategoryEntries(CategoryIndex, false, SingleParts, GroupedParts);

	SingleParts.Sort(lcPartSortFunc);
	mParts.resize(SingleParts.GetSize());

	for (int PartIdx = 0; PartIdx < SingleParts.GetSize(); PartIdx++)
		mParts[PartIdx] = (QPair<PieceInfo*, QPixmap>(SingleParts[PartIdx], QPixmap()));

	endResetModel();
}

int lcPartSelectionListModel::rowCount(const QModelIndex& Parent) const
{
	Q_UNUSED(Parent);
	return mParts.size();
}

QVariant lcPartSelectionListModel::data(const QModelIndex& Index, int Role) const
{
	int InfoIndex = Index.row();

	if (Index.isValid() && InfoIndex < mParts.size())
	{
		if (Role == Qt::ToolTipRole)
			return QVariant(QString::fromLatin1(mParts[InfoIndex].first->m_strDescription));
		else if (Role == Qt::DecorationRole)
		{
			if (!mParts[InfoIndex].second.isNull())
				return QVariant(mParts[InfoIndex].second);
			else
				return QVariant(QColor(0, 0, 0, 0));
		}
	}

	return QVariant();
}

QVariant lcPartSelectionListModel::headerData(int Section, Qt::Orientation Orientation, int Role) const
{
	Q_UNUSED(Section);
	Q_UNUSED(Orientation);
	return Role == Qt::DisplayRole ? QVariant(QStringLiteral("Image")) : QVariant();
}

Qt::ItemFlags lcPartSelectionListModel::flags(const QModelIndex& Index) const
{
	Qt::ItemFlags DefaultFlags = QAbstractListModel::flags(Index);

	if (Index.isValid())
		return Qt::ItemIsDragEnabled | DefaultFlags;
	else
		return DefaultFlags;
}


#include "lc_mainwindow.h"
#include "preview.h"

void lcPartSelectionListModel::RequestPreview(int InfoIndex)
{
	if (mParts[InfoIndex].second.isNull())
	{
		gMainWindow->mPreviewWidget->MakeCurrent();
		lcContext* Context = gMainWindow->mPreviewWidget->mContext;
		int Width = gIconSize;
		int Height = gIconSize;

		if (!Context->BeginRenderToTexture(Width, Height))
			return;

		float aspect = (float)Width / (float)Height;
		Context->SetViewport(0, 0, Width, Height);

		lcMatrix44 ProjectionMatrix = lcMatrix44Perspective(30.0f, aspect, 1.0f, 2500.0f);
		lcMatrix44 ViewMatrix;

		Context->SetDefaultState();
		Context->SetProjectionMatrix(ProjectionMatrix);
		Context->SetProgram(LC_PROGRAM_SIMPLE);

		PieceInfo* Info = mParts[InfoIndex].first;
		Info->AddRef();

		glClearColor(1.0f, 1.0f, 1.0f, 0.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		
		lcVector3 CameraPosition(-100.0f, -100.0f, 75.0f);
		Info->ZoomExtents(ProjectionMatrix, ViewMatrix, CameraPosition);

		lcScene Scene;
		Scene.Begin(ViewMatrix);

		Info->AddRenderMeshes(Scene, lcMatrix44Identity(), gDefaultColor, false, false);

		Scene.End();

		Context->SetViewMatrix(ViewMatrix);
		Context->DrawOpaqueMeshes(Scene.mOpaqueMeshes);
		Context->DrawTranslucentMeshes(Scene.mTranslucentMeshes);

		Context->UnbindMesh(); // context remove
		
		Info->Release();

		mParts[InfoIndex].second = QPixmap::fromImage(Context->GetRenderToTextureImage(Width, Height));

		Context->EndRenderToTexture();

		emit dataChanged(index(InfoIndex, 0), index(InfoIndex, 0), QVector<int>() << Qt::DecorationRole);
	}
}

lcPartSelectionListView::lcPartSelectionListView(QWidget* Parent)
	: QListView(Parent)
{
	setUniformItemSizes(true);
	setViewMode(QListView::IconMode);
	setIconSize(QSize(gIconSize, gIconSize));
	setResizeMode(QListView::Adjust);
	setDragEnabled(true);

	mListModel = new lcPartSelectionListModel(this);
	mFilterModel = new lcPartSelectionFilterModel(this);
	mFilterModel->setSourceModel(mListModel);
	setModel(mFilterModel);
	lcPartSelectionItemDelegate* ItemDelegate = new lcPartSelectionItemDelegate(this, mListModel, mFilterModel);
	setItemDelegate(ItemDelegate);
}

void lcPartSelectionListView::startDrag(Qt::DropActions SupportedActions)
{
	Q_UNUSED(SupportedActions);

	PieceInfo* Info = GetCurrentPart();

	if (!Info)
		return;

	QByteArray ItemData;
	QDataStream DataStream(&ItemData, QIODevice::WriteOnly);
	DataStream << QString(Info->m_strName);

	QMimeData* MimeData = new QMimeData;
	MimeData->setData("application/vnd.leocad-part", ItemData);

	QDrag* Drag = new QDrag(this);
	Drag->setMimeData(MimeData);

	Drag->exec(Qt::CopyAction);
}

lcPartSelectionWidget::lcPartSelectionWidget(QWidget* Parent)
	: QWidget(Parent)
{
	mSplitter = new QSplitter(this);

	mCategoriesWidget = new QTreeWidget(mSplitter);
	mCategoriesWidget->setHeaderHidden(true);
	mCategoriesWidget->setUniformRowHeights(true);
	mCategoriesWidget->setRootIsDecorated(false);

	for (int CategoryIdx = 0; CategoryIdx < gCategories.GetSize(); CategoryIdx++)
	{
		QTreeWidgetItem* CategoryItem = new QTreeWidgetItem(mCategoriesWidget, QStringList((const char*)gCategories[CategoryIdx].Name));
//		CategoryItem->setCheckState(0, Qt::Unchecked);
	}

	QWidget* PartsGroupWidget = new QWidget(mSplitter);

	QVBoxLayout* PartsLayout = new QVBoxLayout();
	PartsLayout->setContentsMargins(0, 0, 0, 0);
	PartsGroupWidget->setLayout(PartsLayout);

	mFilterWidget = new QLineEdit(PartsGroupWidget);
	mFilterWidget->setPlaceholderText(tr("Search Parts"));
	mFilterWidget->addAction(QIcon(":/resources/parts_search.png"), QLineEdit::TrailingPosition);
	PartsLayout->addWidget(mFilterWidget);

	mPartsWidget = new lcPartSelectionListView(PartsGroupWidget);
	PartsLayout->addWidget(mPartsWidget);

	QHBoxLayout* Layout = new QHBoxLayout(this);
	Layout->setContentsMargins(0, 0, 0, 0);
	Layout->addWidget(mSplitter);
	setLayout(Layout);

	connect(mPartsWidget->selectionModel(), &QItemSelectionModel::currentChanged, this, &lcPartSelectionWidget::PartChanged);
	connect(mFilterWidget, &QLineEdit::textEdited, this, &lcPartSelectionWidget::FilterChanged);
	connect(mCategoriesWidget, &QTreeWidget::currentItemChanged, this, &lcPartSelectionWidget::CategoryChanged);
}

lcPartSelectionWidget::~lcPartSelectionWidget()
{
}

void lcPartSelectionWidget::resizeEvent(QResizeEvent* Event)
{
	if (width() > height())
		mSplitter->setOrientation(Qt::Horizontal);
	else
		mSplitter->setOrientation(Qt::Vertical);
	
	QWidget::resizeEvent(Event);
}

void lcPartSelectionWidget::FilterChanged(const QString& Text)
{
	mPartsWidget->GetFilterModel()->SetFilter(Text);
}

void lcPartSelectionWidget::CategoryChanged(QTreeWidgetItem* Current, QTreeWidgetItem* Previous)
{
	Q_UNUSED(Previous);
	mPartsWidget->GetListModel()->SetCategory(mCategoriesWidget->indexOfTopLevelItem(Current));
	mPartsWidget->setCurrentIndex(mPartsWidget->GetFilterModel()->index(0, 0));
}

void lcPartSelectionWidget::PartChanged(const QModelIndex& Current, const QModelIndex& Previous)
{
	gMainWindow->SetCurrentPieceInfo(mPartsWidget->GetCurrentPart());
}