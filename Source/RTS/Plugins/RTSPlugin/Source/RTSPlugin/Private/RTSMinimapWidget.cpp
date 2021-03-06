#include "RTSPluginPrivatePCH.h"
#include "RTSMinimapWidget.h"

#include "Components/BrushComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Rendering/DrawElements.h"

#include "RTSMinimapVolume.h"
#include "RTSOwnerComponent.h"
#include "RTSPlayerController.h"
#include "RTSPlayerState.h"


URTSMinimapWidget::URTSMinimapWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void URTSMinimapWidget::NotifyOnDrawUnit(
	FPaintContext& Context,
	AActor* Actor,
	APlayerState* ActorOwner,
	const FVector2D& MinimapPosition,
	APlayerState* LocalPlayer) const
{
	ReceiveOnDrawUnit(Context, Actor, ActorOwner, MinimapPosition, LocalPlayer);
}

void URTSMinimapWidget::NativeConstruct()
{
	UUserWidget::NativeConstruct();

	// Get minimap bounds.
	for (TActorIterator<ARTSMinimapVolume> It(GetWorld()); It; ++It)
	{
		MinimapVolume = *It;
		break;
	}

	if (!MinimapVolume)
	{
		UE_LOG(RTSLog, Warning, TEXT("No RTSMinimapVolume found. Minimap won't be showing unit positions."));
		return;
	}

	UBrushComponent* MinimapBrushComponent = MinimapVolume->GetBrushComponent();
	FBoxSphereBounds MinimapBounds = MinimapBrushComponent->CalcBounds(MinimapBrushComponent->GetComponentTransform());

	MinimapWorldSize = MinimapBounds.BoxExtent * 2;
}

void URTSMinimapWidget::NativePaint(FPaintContext& InContext) const
{
	UUserWidget::NativePaint(InContext);

	InContext.MaxLayer++;

	DrawBackground(InContext);
	DrawUnits(InContext);
	DrawViewFrustum(InContext);
}

FReply URTSMinimapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Apply default handling.
	auto Reply = UUserWidget::NativeOnMouseButtonDown(InGeometry, InMouseEvent);

	if (Reply.IsEventHandled())
	{
		return Reply;
	}

	// Handle MouseMove events from now on.
	bMouseDown = true;

	// Handle initial click.
	return HandleMinimapClick(InGeometry, InMouseEvent);
}

FReply URTSMinimapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Apply default handling.
	auto Reply = UUserWidget::NativeOnMouseButtonUp(InGeometry, InMouseEvent);

	if (Reply.IsEventHandled())
	{
		return Reply;
	}

	// Stop handling MouseMove events.
	bMouseDown = false;
	return FReply::Handled();
}

FReply URTSMinimapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Apply default handling.
	auto Reply = UUserWidget::NativeOnMouseMove(InGeometry, InMouseEvent);

	if (Reply.IsEventHandled())
	{
		return Reply;
	}

	if (bMouseDown)
	{
		// Handle movement.
		return HandleMinimapClick(InGeometry, InMouseEvent);
	}

	return FReply::Unhandled();
}

void URTSMinimapWidget::DrawBackground(FPaintContext& InContext) const
{
	if (!bDrawBackground)
	{
		return;
	}

	DrawBoxWithBrush(InContext, FVector2D(0, 0), MinimapBackground);
}

void URTSMinimapWidget::DrawUnits(FPaintContext& InContext) const
{
	if (!MinimapVolume)
	{
		return;
	}

	APlayerController* Player = GetOwningPlayer();

	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		URTSOwnerComponent* OwnerComponent = Actor->FindComponentByClass<URTSOwnerComponent>();

		FVector ActorLocationWorld = Actor->GetActorLocation();
		FVector2D ActorLocationMinimap = WorldToMinimap(ActorLocationWorld);
		
		// Draw on minimap.
		if (bDrawUnitsWithTeamColors && OwnerComponent)
		{
			if (OwnerComponent->GetPlayerOwner() == Player->PlayerState)
			{
				DrawBoxWithBrush(InContext, ActorLocationMinimap, OwnUnitsBrush);
			}
			else if (OwnerComponent->GetPlayerOwner() != nullptr && !OwnerComponent->IsSameTeamAsController(Player))
			{
				DrawBoxWithBrush(InContext, ActorLocationMinimap, EnemyUnitsBrush);
			}
			else
			{
				DrawBoxWithBrush(InContext, ActorLocationMinimap, NeutralUnitsBrush);
			}
		}
		
		// Allow custom drawing.
		NotifyOnDrawUnit(
			InContext,
			Actor,
			OwnerComponent ? OwnerComponent->GetPlayerOwner() : nullptr,
			ActorLocationMinimap,
			Player->PlayerState);
	}
}

void URTSMinimapWidget::DrawViewFrustum(FPaintContext& InContext) const
{
	if (!bDrawViewFrustum)
	{
		return;
	}

	// Get viewport size.
	ARTSPlayerController* Player = Cast<ARTSPlayerController>(GetOwningPlayer());

	if (!Player)
	{
		return;
	}

	int32 ViewportWidth;
	int32 ViewportHeight;

	Player->GetViewportSize(ViewportWidth, ViewportHeight);

	// Cast four rays.
	FVector2D ViewportTopLeft(0, 0);
	FVector2D ViewportTopRight(ViewportWidth, 0);
	FVector2D ViewportBottomLeft(0, ViewportHeight);
	FVector2D ViewportBottomRight(ViewportWidth, ViewportHeight);

	FVector WorldTopLeft;
	FVector WorldTopRight;
	FVector WorldBottomLeft;
	FVector WorldBottomRight;

	ViewportToWorld(Player, ViewportTopLeft, WorldTopLeft);
	ViewportToWorld(Player, ViewportTopRight, WorldTopRight);
	ViewportToWorld(Player, ViewportBottomLeft, WorldBottomLeft);
	ViewportToWorld(Player, ViewportBottomRight, WorldBottomRight);

	// Convert to minimap space.
	FVector2D MinimapTopLeft = WorldToMinimap(WorldTopLeft);
	FVector2D MinimapTopRight = WorldToMinimap(WorldTopRight);
	FVector2D MinimapBottomLeft = WorldToMinimap(WorldBottomLeft);
	FVector2D MinimapBottomRight = WorldToMinimap(WorldBottomRight);

	// Draw view frustum.
	TArray<FVector2D> Points;

	Points.Add(MinimapTopLeft);
	Points.Add(MinimapTopRight);
	Points.Add(MinimapBottomRight);
	Points.Add(MinimapBottomLeft);
	Points.Add(MinimapTopLeft);

	FSlateDrawElement::MakeLines(
		InContext.OutDrawElements,
		InContext.MaxLayer,
		InContext.AllottedGeometry.ToPaintGeometry(),
		Points);
}

void URTSMinimapWidget::DrawBoxWithBrush(FPaintContext& InContext, const FVector2D& Position, const FSlateBrush& Brush) const
{
	FSlateDrawElement::MakeBox(
		InContext.OutDrawElements,
		InContext.MaxLayer,
		InContext.AllottedGeometry.ToPaintGeometry(Position, Brush.ImageSize),
		&Brush,
		ESlateDrawEffect::None,
		Brush.TintColor.GetSpecifiedColor());
}

FReply URTSMinimapWidget::HandleMinimapClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	ARTSPlayerController* Player = Cast<ARTSPlayerController>(GetOwningPlayer());

	if (!Player)
	{
		FReply::Unhandled();
	}

	// Convert clicked minimap position to world space.
	FVector2D MinimapPosition = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	FVector WorldPosition = MinimapToWorld(MinimapPosition);

	// Notify player.
	Player->NotifyOnMinimapClicked(InMouseEvent, MinimapPosition, WorldPosition);
	return FReply::Handled();
}

FVector URTSMinimapWidget::MinimapToWorld(const FVector2D& MinimapPosition) const
{
	// Convert to relative minimap position.
	float RelativeMinimapX = MinimapPosition.X / MinimapBackground.ImageSize.X;
	float RelativeMinimapY = MinimapPosition.Y / MinimapBackground.ImageSize.Y;

	// Rotate to match UI coordinate system.
	float temp = RelativeMinimapX;
	RelativeMinimapX = 1 - RelativeMinimapY;
	RelativeMinimapY = temp;

	// Convert to world coordinates.
	float WorldX = (RelativeMinimapX - 0.5) * MinimapWorldSize.X;
	float WorldY = (RelativeMinimapY - 0.5) * MinimapWorldSize.Y;

	return FVector(WorldX, WorldY, 0.0f);
}

bool URTSMinimapWidget::ViewportToWorld(ARTSPlayerController* Player, const FVector2D& ViewportPosition, FVector& OutWorldPosition) const
{
	// Get ray.
	FVector WorldOrigin;
	FVector WorldDirection;
	if (!UGameplayStatics::DeprojectScreenToWorld(Player, ViewportPosition, WorldOrigin, WorldDirection))
	{
		return false;
	}

	// Make plane.
	FPlane ZPlane = FPlane(FVector::ZeroVector, FVector::UpVector);

	// Calculate intersection point.
	OutWorldPosition = FMath::LinePlaneIntersection(WorldOrigin, WorldOrigin + WorldDirection * 1000.0f, ZPlane);
	return true;
}

FVector2D URTSMinimapWidget::WorldToMinimap(const FVector& WorldPosition) const
{
	// Get relative world position.
	float RelativeWorldX = WorldPosition.X / MinimapWorldSize.X + 0.5f;
	float RelativeWorldY = WorldPosition.Y / MinimapWorldSize.Y + 0.5f;

	// Rotate to match UI coordinate system.
	float temp = RelativeWorldX;
	RelativeWorldX = RelativeWorldY;
	RelativeWorldY = 1 - temp;

	// Convert to minimap coordinates.
	float MinimapX = RelativeWorldX * MinimapBackground.ImageSize.X;
	float MinimapY = RelativeWorldY * MinimapBackground.ImageSize.Y;

	return FVector2D(MinimapX, MinimapY);
}
