#include "ShadeHUD.h"
#include "shadeNseek.h"
#include "ShadeTypes.h"
#include "ShadeAssets.h"
#include "ShadeGameMode.h"
#include "ShadePlayerController.h"
#include "ShadeCharacter.h"
#include "ShadeInteractables.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBackgroundBlur.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Fonts/CompositeFont.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/Paths.h"

#define LOCTEXT_NAMESPACE "ShadeHUD"

// =====================================================================================================================
// Design tokens: sun-bleached Mediterranean - cream plaster, terracotta, lemon, sea blue, shadow violet.
// =====================================================================================================================
namespace UI
{
	const FLinearColor Cream(0.97f, 0.92f, 0.82f, 1.f);
	const FLinearColor Paper(0.95f, 0.89f, 0.76f, 1.f);
	const FLinearColor Ink(0.07f, 0.11f, 0.19f, 1.f);
	const FLinearColor InkSoft(0.22f, 0.25f, 0.32f, 1.f);
	const FLinearColor Terracotta(0.78f, 0.33f, 0.17f, 1.f);
	const FLinearColor Lemon(1.f, 0.80f, 0.22f, 1.f);
	const FLinearColor Sea(0.10f, 0.40f, 0.62f, 1.f);
	const FLinearColor Olive(0.42f, 0.48f, 0.22f, 1.f);
	const FLinearColor Violet(0.17f, 0.12f, 0.28f, 1.f);
	const FLinearColor Sun(1.f, 0.62f, 0.2f, 1.f);
	const FLinearColor Black(0.f, 0.f, 0.f, 1.f);
	constexpr float Margin = 56.f;
}

namespace
{
	const FSlateColorBrush GSolid(FLinearColor::White);
	const FSlateRoundedBoxBrush GRound6(FLinearColor::White, 6.f);
	const FSlateRoundedBoxBrush GRound14(FLinearColor::White, 14.f);
	const FSlateRoundedBoxBrush GRound30(FLinearColor::White, 30.f);
	const FSlateRoundedBoxBrush GOutline6(FLinearColor::Transparent, 6.f, FLinearColor::White, 1.5f);
	const FSlateRoundedBoxBrush GOutline14(FLinearColor::Transparent, 14.f, FLinearColor::White, 1.5f);

	FLinearColor A(FLinearColor C, float Alpha)
	{
		C.A *= FMath::Clamp(Alpha, 0.f, 1.f);
		return C;
	}

	struct FPainter
	{
		const FGeometry& Geo;
		FSlateWindowElementList& Out;
		int32 Layer;
		float S;
		FVector2D Size;
		FSlateResourceHandle Solid;

		FPainter(const FGeometry& InGeo, FSlateWindowElementList& InOut, int32 InLayer)
			: Geo(InGeo), Out(InOut), Layer(InLayer)
		{
			const FVector2D Local = InGeo.GetLocalSize();
			S = FMath::Max(Local.Y / 1080.f, 0.1f);
			Size = Local / S;
			Solid = FSlateApplication::Get().GetRenderer()->GetResourceHandle(GSolid);
		}

		FPaintGeometry PG(const FVector2D& Pos, const FVector2D& Sz) const
		{
			return Geo.ToPaintGeometry(FVector2f(Sz * S), FSlateLayoutTransform(FVector2f(Pos * S)));
		}

		void Box(const FVector2D& Pos, const FVector2D& Sz, const FLinearColor& C, const FSlateBrush* Brush = &GSolid, int32 L = 0) const
		{
			if (C.A <= 0.002f || Sz.X <= 0.f || Sz.Y <= 0.f) { return; }
			FSlateDrawElement::MakeBox(Out, Layer + L, PG(Pos, Sz), Brush, ESlateDrawEffect::None, C);
		}

		void Gradient(const FVector2D& Pos, const FVector2D& Sz, const FLinearColor& From, const FLinearColor& To, EOrientation Orient, int32 L = 0) const
		{
			if (From.A <= 0.002f && To.A <= 0.002f) { return; }
			TArray<FSlateGradientStop> Stops;
			Stops.Add(FSlateGradientStop(FVector2D::ZeroVector, From));
			Stops.Add(FSlateGradientStop(Sz * S, To));
			FSlateDrawElement::MakeGradient(Out, Layer + L, PG(Pos, Sz), Stops, Orient);
		}

		void Poly(const TArray<FVector2D>& Pts, const FLinearColor& C, int32 L = 0) const
		{
			if (C.A <= 0.002f || Pts.Num() < 3) { return; }
			const FSlateRenderTransform& RT = Geo.GetAccumulatedRenderTransform();
			const FColor Col = C.ToFColor(true);
			TArray<FSlateVertex> Verts;
			TArray<SlateIndex> Idx;
			for (const FVector2D& P : Pts)
			{
				Verts.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(RT, FVector2f(P * S), FVector2f(0.5f, 0.5f), Col));
			}
			for (int32 i = 1; i + 1 < Pts.Num(); ++i)
			{
				Idx.Add(0);
				Idx.Add(i);
				Idx.Add(i + 1);
			}
			FSlateDrawElement::MakeCustomVerts(Out, Layer + L, Solid, Verts, Idx, nullptr, 0, 0);
		}

		void Circle(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 0, int32 Seg = 28) const
		{
			TArray<FVector2D> Pts;
			for (int32 i = 0; i < Seg; ++i)
			{
				const float Ang = 2.f * PI * i / Seg;
				Pts.Add(C + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
			}
			Poly(Pts, Col, L);
		}

		void Diamond(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 0) const
		{
			Poly({ C + FVector2D(0, -R), C + FVector2D(R, 0), C + FVector2D(0, R), C + FVector2D(-R, 0) }, Col, L);
		}

		void Lines(const TArray<FVector2D>& Pts, const FLinearColor& C, float T = 1.5f, int32 L = 1) const
		{
			if (C.A <= 0.002f || Pts.Num() < 2) { return; }
			TArray<FVector2f> Local;
			Local.Reserve(Pts.Num());
			for (const FVector2D& P : Pts) { Local.Add(FVector2f(P * S)); }
			FSlateDrawElement::MakeLines(Out, Layer + L, Geo.ToPaintGeometry(), Local, ESlateDrawEffect::None, C, true, T * S);
		}

		void Arc(const FVector2D& Center, float Radius, float From01, float To01, const FLinearColor& C, float T, int32 L = 2) const
		{
			TArray<FVector2D> Pts;
			const int32 Seg = FMath::Max(2, FMath::CeilToInt(FMath::Abs(To01 - From01) * 64.f));
			for (int32 i = 0; i <= Seg; ++i)
			{
				const float Ang = (-0.25f + FMath::Lerp(From01, To01, static_cast<float>(i) / Seg)) * 2.f * PI;
				Pts.Add(Center + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * Radius);
			}
			Lines(Pts, C, T, L);
		}

		/** Soft dark backing (stacked rounded boxes fake a blur falloff). */
		void Soft(const FVector2D& Pos, const FVector2D& Sz, float Alpha, int32 Steps = 14, const FLinearColor& Tint = FLinearColor(0.02f, 0.03f, 0.06f, 1.f)) const
		{
			const float Inset = 3.f;
			for (int32 i = 0; i < Steps; ++i)
			{
				const FVector2D O(Inset * i, Inset * i);
				if (Sz.X - O.X * 2.f <= 0.f || Sz.Y - O.Y * 2.f <= 0.f) { break; }
				Box(Pos + O, Sz - O * 2.f, FLinearColor(Tint.R, Tint.G, Tint.B, Alpha / Steps * 1.5f), &GRound30);
			}
		}

		FVector2D Measure(const FString& Str, const FSlateFontInfo& Font) const
		{
			const TSharedRef<FSlateFontMeasure> M = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
			return M->Measure(Str, Font) / S;
		}

		/** Align 0 = left/top, 0.5 = centre, 1 = right/bottom. */
		FVector2D Text(const FString& Str, const FVector2D& Pos, const FSlateFontInfo& Font, const FLinearColor& C, float AX = 0.f, float AY = 0.f, float ShadowA = 0.45f, int32 L = 3) const
		{
			const FVector2D Sz = Measure(Str, Font);
			if (C.A > 0.002f && !Str.IsEmpty())
			{
				const FVector2D At(Pos.X - Sz.X * AX, Pos.Y - Sz.Y * AY);
				if (ShadowA > 0.f)
				{
					FSlateDrawElement::MakeText(Out, Layer + L - 1, PG(At + FVector2D(1.5f, 2.f), Sz), Str, Font, ESlateDrawEffect::None, FLinearColor(0.02f, 0.02f, 0.05f, ShadowA * C.A));
				}
				FSlateDrawElement::MakeText(Out, Layer + L, PG(At, Sz), Str, Font, ESlateDrawEffect::None, C);
			}
			return Sz;
		}

		/** A glazed-tile card: cream body, terracotta rim, painted corner diamonds. */
		void Tile(const FVector2D& Pos, const FVector2D& Sz, float Alpha, const FLinearColor& Rim = UI::Terracotta) const
		{
			Box(Pos + FVector2D(3.f, 5.f), Sz, A(FLinearColor(0.f, 0.f, 0.02f, 1.f), 0.28f * Alpha), &GRound14, 0);
			Box(Pos, Sz, A(UI::Cream, 0.96f * Alpha), &GRound14, 1);
			Box(Pos + FVector2D(5.f, 5.f), Sz - FVector2D(10.f, 10.f), A(Rim, 0.85f * Alpha), &GOutline14, 2);
			const float R = 5.f;
			for (const FVector2D& Corner : { Pos + FVector2D(14, 14), Pos + FVector2D(Sz.X - 14, 14), Pos + FVector2D(14, Sz.Y - 14), Pos + Sz - FVector2D(14, 14) })
			{
				Diamond(Corner, R, A(UI::Sea, 0.8f * Alpha), 3);
				Diamond(Corner, R * 0.45f, A(UI::Lemon, 0.9f * Alpha), 4);
			}
		}

		/** Keycap. Returns width. */
		float Key(const FString& Label, const FVector2D& Pos, const FSlateFontInfo& Font, float Alpha, float Height = 30.f) const
		{
			const FVector2D T = Measure(Label, Font);
			const float W = FMath::Max(Height, static_cast<float>(T.X) + 18.f);
			Box(Pos + FVector2D(0.f, 3.f), FVector2D(W, Height), A(UI::Ink, 0.55f * Alpha), &GRound6, 3);
			Box(Pos, FVector2D(W, Height), A(UI::Paper, Alpha), &GRound6, 4);
			Box(Pos, FVector2D(W, Height), A(UI::Ink, 0.75f * Alpha), &GOutline6, 5);
			Text(Label, Pos + FVector2D(W * 0.5f, Height * 0.5f), Font, A(UI::Ink, Alpha), 0.5f, 0.5f, 0.f, 6);
			return W;
		}

		/** Tiny painted sun: disc + rays; Fill tints it from pale to blazing. */
		void SunGlyph(const FVector2D& C, float R, float Fill, float Alpha, float Spin) const
		{
			const FLinearColor Col = FMath::Lerp(UI::Lemon, UI::Sun, Fill);
			for (int32 i = 0; i < 10; ++i)
			{
				const float Ang = Spin + i * 2.f * PI / 10.f;
				const FVector2D D(FMath::Cos(Ang), FMath::Sin(Ang));
				const FVector2D N(-D.Y, D.X);
				const float L0 = R * 1.25f, L1 = R * (1.65f + 0.25f * Fill);
				Poly({ C + D * L0 + N * R * 0.16f, C + D * L1, C + D * L0 - N * R * 0.16f }, A(Col, Alpha), 3);
			}
			Circle(C, R, A(Col, Alpha), 3);
			Circle(C, R * 0.62f, A(FMath::Lerp(UI::Cream, UI::Lemon, Fill), Alpha), 4);
		}

		/** Little cat-head silhouette (for Shade / Biscotto icons). */
		void CatGlyph(const FVector2D& C, float R, const FLinearColor& Col, int32 L = 3) const
		{
			Circle(C, R, Col, L);
			Poly({ C + FVector2D(-R * 0.95f, -R * 0.2f), C + FVector2D(-R * 0.75f, -R * 1.35f), C + FVector2D(-R * 0.15f, -R * 0.8f) }, Col, L);
			Poly({ C + FVector2D(R * 0.95f, -R * 0.2f), C + FVector2D(R * 0.75f, -R * 1.35f), C + FVector2D(R * 0.15f, -R * 0.8f) }, Col, L);
		}
	};

	FString FormatTime(float Seconds)
	{
		const int32 T = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), T / 60, T % 60);
	}
}

// =====================================================================================================================
// Overlay widget
// =====================================================================================================================

class SShadeOverlay : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SShadeOverlay) {}
		SLATE_ARGUMENT(TWeakObjectPtr<AShadeHUD>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args)
	{
		Owner = Args._Owner;
		SetCanTick(true);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(1920.f, 1080.f); }
	virtual bool SupportsKeyboardFocus() const override { return true; }

	virtual void Tick(const FGeometry& Geo, const double CurrentTime, const float DeltaTime) override
	{
		SLeafWidget::Tick(Geo, CurrentTime, DeltaTime);
		Dt = FMath::Clamp(DeltaTime, 0.f, 0.1f);
		UpdateSmoothing();
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Mouse) override;
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Mouse) override;

private:
	AShadeGameMode* GM() const
	{
		AShadeHUD* H = Owner.Get();
		return H && H->GetWorld() ? H->GetWorld()->GetAuthGameMode<AShadeGameMode>() : nullptr;
	}
	AShadePlayerController* PC() const
	{
		AShadeHUD* H = Owner.Get();
		return H ? Cast<AShadePlayerController>(H->GetOwningPlayerController()) : nullptr;
	}
	AShadeCharacter* Shade() const
	{
		AShadePlayerController* P = PC();
		return P ? Cast<AShadeCharacter>(P->GetPawn()) : nullptr;
	}
	FSlateFontInfo F(FName Face, float Size, int32 Spacing, float S) const
	{
		AShadeHUD* H = Owner.Get();
		return H ? H->Font(Face, Size * S, Spacing) : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), FMath::RoundToInt(Size * S));
	}
	FString KeyLabel(const TCHAR* Kbd, const TCHAR* Pad) const
	{
		const AShadePlayerController* P = PC();
		return (P && P->IsUsingGamepad()) ? FString(Pad) : FString(Kbd);
	}
	bool Project(const FPainter& P, const FVector& World, FVector2D& Out) const;
	void UpdateSmoothing();

	void PaintObjective(const FPainter& P, double Now) const;
	void PaintPrompt(const FPainter& P, double Now) const;
	void PaintMarker(const FPainter& P, double Now) const;
	void PaintExposure(const FPainter& P, double Now) const;
	void PaintLine(const FPainter& P, double Now) const;
	void PaintToasts(const FPainter& P, double Now) const;
	void PaintControls(const FPainter& P, float Alpha, const FVector2D& Origin) const;
	void PaintTitle(const FPainter& P, double Now) const;
	void PaintPause(const FPainter& P, double Now) const;
	void PaintEnd(const FPainter& P, double Now) const;
	void PaintMenuItems(const FPainter& P, double Now, const FVector2D& Center, float Alpha) const;

	TWeakObjectPtr<AShadeHUD> Owner;
	float Dt = 0.f;
	float HudAlpha = 0.f;
	float PromptAlpha = 0.f;
	float MarkerAlpha = 0.f;
	float ExposureShown = 0.f;
	float FlashAlpha = 0.f;
	bool bWasEvap = false;
	TWeakObjectPtr<AShadeInteractable> PromptTarget;
	mutable TArray<FBox2D> ItemRects;
	mutable float LastS = 1.f;
};

void SShadeOverlay::UpdateSmoothing()
{
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	if (!G || !K)
	{
		return;
	}
	const bool bPlay = G->GetPhase() == EShadePhase::Playing || G->GetPhase() == EShadePhase::Finale;
	HudAlpha = ShadeEase::Approach(HudAlpha, bPlay ? 1.f : 0.f, 3.f, Dt);
	AShadeInteractable* I = bPlay ? K->GetFocused() : nullptr;
	if (I && !I->Verb.IsEmpty())
	{
		PromptTarget = I;
	}
	const bool bPrompt = I && !I->Verb.IsEmpty();
	PromptAlpha = ShadeEase::Approach(PromptAlpha, bPrompt ? 1.f : 0.f, bPrompt ? 10.f : 7.f, Dt);
	MarkerAlpha = ShadeEase::Approach(MarkerAlpha, (bPlay && !bPrompt) ? 1.f : 0.f, 4.f, Dt);
	ExposureShown = ShadeEase::Approach(ExposureShown, K->GetExposure(), 14.f, Dt);
	if (K->IsEvaporating() && !bWasEvap)
	{
		FlashAlpha = 1.f;
	}
	bWasEvap = K->IsEvaporating();
	FlashAlpha = FMath::Max(0.f, FlashAlpha - Dt * 1.6f);
}

bool SShadeOverlay::Project(const FPainter& P, const FVector& World, FVector2D& Out) const
{
	AShadePlayerController* Ctrl = PC();
	if (!Ctrl || !Ctrl->PlayerCameraManager)
	{
		return false;
	}
	FVector2D Viewport(1920.f, 1080.f);
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->GetViewportSize(Viewport);
	}
	const FVector Local = Ctrl->PlayerCameraManager->GetCameraRotation().UnrotateVector(World - Ctrl->PlayerCameraManager->GetCameraLocation());
	FVector2D Screen;
	if (Local.X < 10.f || !Ctrl->ProjectWorldLocationToScreen(World, Screen, true))
	{
		return false;
	}
	Out = Screen / Viewport * P.Size;
	return Out.X > 40.f && Out.Y > 40.f && Out.X < P.Size.X - 40.f && Out.Y < P.Size.Y - 40.f;
}

int32 SShadeOverlay::OnPaint(const FPaintArgs& Args, const FGeometry& Geo, const FSlateRect& Culling, FSlateWindowElementList& Out, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const
{
	FPainter P(Geo, Out, LayerId);
	LastS = P.S;
	const double Now = FPlatformTime::Seconds();
	ItemRects.Reset();
	AShadeGameMode* G = GM();
	AShadeHUD* H = Owner.Get();
	if (!G || !H)
	{
		return LayerId;
	}

	// Letterbox (cinematics).
	const float LB = ShadeEase::InOutCubic(G->GetLetterbox()) * 0.1f * P.Size.Y;
	if (LB > 0.5f)
	{
		P.Box(FVector2D(0, 0), FVector2D(P.Size.X, LB), FLinearColor(0.01f, 0.01f, 0.02f, 1.f), &GSolid, 20);
		P.Box(FVector2D(0, P.Size.Y - LB), FVector2D(P.Size.X, LB), FLinearColor(0.01f, 0.01f, 0.02f, 1.f), &GSolid, 20);
	}

	PaintExposure(P, Now);
	if (HudAlpha > 0.01f)
	{
		PaintObjective(P, Now);
		PaintMarker(P, Now);
		PaintPrompt(P, Now);
		PaintToasts(P, Now);
		const float Intro = ShadeEase::Window(G->GetPhaseTime(), 0.5f, 1.5f) * (1.f - ShadeEase::Window(G->GetRunTime(), 16.f, 18.f));
		if (G->GetPhase() == EShadePhase::Playing && Intro > 0.01f)
		{
			PaintControls(P, Intro * HudAlpha, FVector2D(UI::Margin, P.Size.Y - UI::Margin - 40.f));
		}
	}
	PaintLine(P, Now);

	switch (H->GetMenu())
	{
	case EShadeMenu::Title: PaintTitle(P, Now); break;
	case EShadeMenu::Pause: PaintPause(P, Now); break;
	case EShadeMenu::End: PaintEnd(P, Now); break;
	default: break;
	}

	// Evaporation flash + fade.
	if (FlashAlpha > 0.f)
	{
		P.Box(FVector2D(0, 0), P.Size, FLinearColor(1.f, 0.86f, 0.62f, 0.55f * FlashAlpha * FlashAlpha), &GSolid, 30);
	}
	if (G->GetFadeAlpha() > 0.002f)
	{
		P.Box(FVector2D(0, 0), P.Size, A(G->GetFadeColor(), G->GetFadeAlpha()), &GSolid, 40);
	}
	return LayerId + 50;
}

void SShadeOverlay::PaintObjective(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	const FShadeObjective* O = G ? G->GetObjective() : nullptr;
	if (!O)
	{
		return;
	}
	const float S = P.S;
	const float Since = static_cast<float>(Now - G->GetObjectiveChangeTime());
	const float In = ShadeEase::OutBack(ShadeEase::Window(Since, 0.f, 0.55f));
	const float Al = HudAlpha * ShadeEase::Window(Since, 0.f, 0.3f);
	const FSlateFontInfo Head = F(TEXT("Black"), 13, 260, S);
	const FSlateFontInfo Title = F(TEXT("Script"), 30, 0, S);
	const FSlateFontInfo Hint = F(TEXT("Hand"), 19, 0, S);

	const float W = 430.f;
	// Wrap hint into lines that fit.
	TArray<FString> Lines;
	{
		TArray<FString> Words;
		O->Hint.ToString().ParseIntoArray(Words, TEXT(" "));
		FString Cur;
		for (const FString& Wd : Words)
		{
			const FString Try = Cur.IsEmpty() ? Wd : Cur + TEXT(" ") + Wd;
			if (P.Measure(Try, Hint).X > W - 64.f && !Cur.IsEmpty())
			{
				Lines.Add(Cur);
				Cur = Wd;
			}
			else
			{
				Cur = Try;
			}
		}
		if (!Cur.IsEmpty()) { Lines.Add(Cur); }
	}
	const float H = 108.f + Lines.Num() * 26.f;
	const FVector2D Pos(UI::Margin - (1.f - In) * 40.f, UI::Margin);
	P.Tile(Pos, FVector2D(W, H), Al);
	P.Text(TEXT("ON THE WAY HOME"), Pos + FVector2D(32.f, 26.f), Head, A(UI::Terracotta, Al), 0.f, 0.f, 0.f, 5);
	P.Text(O->Title.ToString(), Pos + FVector2D(30.f, 42.f), Title, A(UI::Ink, Al), 0.f, 0.f, 0.f, 5);
	for (int32 i = 0; i < Lines.Num(); ++i)
	{
		P.Text(Lines[i], Pos + FVector2D(32.f, 86.f + i * 26.f), Hint, A(UI::InkSoft, Al), 0.f, 0.f, 0.f, 5);
	}
	// Progress: one painted dot per step.
	const int32 N = G->GetObjectiveCount();
	for (int32 i = 0; i < N; ++i)
	{
		const FVector2D C = Pos + FVector2D(W - 34.f - (N - 1 - i) * 16.f, 32.f);
		const bool bDone = i < G->GetObjectiveIndex();
		const bool bCur = i == G->GetObjectiveIndex();
		P.Circle(C, bCur ? 5.5f : 4.5f, A(bDone ? UI::Olive : (bCur ? UI::Terracotta : FLinearColor(0.6f, 0.55f, 0.48f, 1.f)), Al), 5);
	}
	// New objective shimmer.
	if (Since < 1.2f)
	{
		const float Sh = ShadeEase::Bump(Since, 0.f, 1.2f);
		P.Box(Pos - FVector2D(4.f, 4.f), FVector2D(W + 8.f, H + 8.f), A(UI::Lemon, 0.6f * Sh), &GOutline14, 6);
	}
}

void SShadeOverlay::PaintMarker(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	FVector Target;
	if (!G || !K || MarkerAlpha < 0.02f || !G->GetObjectiveTargetLocation(Target))
	{
		return;
	}
	const float Dist = FVector::Dist(K->GetActorLocation(), Target);
	const float Al = MarkerAlpha * ShadeEase::Window(Dist, 250.f, 600.f) * 0.9f;
	FVector2D At;
	if (Al < 0.02f || !Project(P, Target + FVector(0, 0, 40.f), At))
	{
		return;
	}
	const float Bob = FMath::Sin(static_cast<float>(Now) * 2.4f) * 4.f;
	const FVector2D C = At + FVector2D(0.f, Bob);
	P.Diamond(C + FVector2D(2.f, 3.f), 12.f, A(UI::Black, 0.35f * Al), 2);
	P.Diamond(C, 12.f, A(UI::Cream, Al), 3);
	P.Diamond(C, 7.f, A(UI::Terracotta, Al), 4);
	const FSlateFontInfo Small = F(TEXT("Bold"), 13, 120, P.S);
	P.Text(FString::Printf(TEXT("%d m"), FMath::RoundToInt(Dist / 100.f)), C + FVector2D(0.f, 18.f), Small, A(UI::Cream, Al), 0.5f, 0.f, 0.6f);
}

void SShadeOverlay::PaintPrompt(const FPainter& P, double Now) const
{
	AShadeInteractable* I = PromptTarget.Get();
	AShadeCharacter* K = Shade();
	if (!I || !K || PromptAlpha < 0.02f)
	{
		return;
	}
	FVector2D At;
	if (!Project(P, I->GetFocusLocation(), At))
	{
		At = FVector2D(P.Size.X * 0.5f, P.Size.Y * 0.72f);
	}
	const float Al = PromptAlpha * HudAlpha;
	const float S = P.S;
	const FSlateFontInfo KeyF = F(TEXT("Black"), 15, 0, S);
	const FSlateFontInfo VerbF = F(TEXT("Script"), 24, 0, S);
	const FSlateFontInfo LabelF = F(TEXT("Bold"), 12, 200, S);

	const bool bGrab = K->GetGrabbed() == I;
	const bool bHold = I->IsHoldInteraction();
	FString Verb = bGrab ? FString(TEXT("Let go")) : I->Verb.ToString();
	if (bHold) { Verb += TEXT(" (hold)"); }
	const FString Label = I->Label.ToString().ToUpper();
	const FVector2D VerbSz = P.Measure(Verb, VerbF);
	const FVector2D LabelSz = P.Measure(Label, LabelF);
	const float W = 64.f + FMath::Max(VerbSz.X, LabelSz.X) + 26.f;
	const float H = 58.f;
	const FVector2D Pos(At.X - W * 0.5f, At.Y - H - 18.f - (1.f - PromptAlpha) * 10.f);
	// Pill with a little pointer.
	P.Box(Pos + FVector2D(2.f, 4.f), FVector2D(W, H), A(UI::Black, 0.25f * Al), &GRound30, 2);
	P.Box(Pos, FVector2D(W, H), A(UI::Cream, 0.95f * Al), &GRound30, 3);
	P.Poly({ FVector2D(At.X - 8.f, Pos.Y + H - 1.f), FVector2D(At.X + 8.f, Pos.Y + H - 1.f), FVector2D(At.X, Pos.Y + H + 10.f) }, A(UI::Cream, 0.95f * Al), 3);
	const float KW = P.Key(KeyLabel(TEXT("E"), TEXT("X")), Pos + FVector2D(16.f, 14.f), KeyF, Al, 30.f);
	if (bHold || bGrab)
	{
		const float Prog = I->GetProgress();
		const FVector2D KC = Pos + FVector2D(16.f + KW * 0.5f, 29.f);
		P.Arc(KC, 23.f, 0.f, 1.f, A(UI::InkSoft, 0.25f * Al), 3.f, 6);
		P.Arc(KC, 23.f, 0.f, FMath::Max(Prog, 0.001f), A(UI::Terracotta, Al), 3.5f, 7);
	}
	P.Text(Verb, Pos + FVector2D(26.f + KW, 6.f), VerbF, A(UI::Ink, Al), 0.f, 0.f, 0.f, 5);
	P.Text(Label, Pos + FVector2D(28.f + KW, 38.f), LabelF, A(UI::Terracotta, Al), 0.f, 0.f, 0.f, 5);
}

void SShadeOverlay::PaintExposure(const FPainter& P, double Now) const
{
	AShadeCharacter* K = Shade();
	if (!K)
	{
		return;
	}
	const float E = ExposureShown;
	if (E < 0.005f)
	{
		return;
	}
	// Warm heat creeping in from the screen edges.
	const float Edge = FMath::Pow(E, 1.3f);
	const float D = 300.f * (0.4f + 0.6f * Edge);
	const FLinearColor Hot(1.f, 0.55f, 0.15f, 0.55f * Edge);
	const FLinearColor Clear(1.f, 0.55f, 0.15f, 0.f);
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X, D), Hot, Clear, Orient_Horizontal, 8);
	P.Gradient(FVector2D(0, P.Size.Y - D), FVector2D(P.Size.X, D), Clear, Hot, Orient_Horizontal, 8);
	P.Gradient(FVector2D(0, 0), FVector2D(D, P.Size.Y), Hot, Clear, Orient_Vertical, 8);
	P.Gradient(FVector2D(P.Size.X - D, 0), FVector2D(D, P.Size.Y), Clear, Hot, Orient_Vertical, 8);

	// Sun glyph with a burning ring under Shade.
	const FVector2D C(P.Size.X * 0.5f, P.Size.Y - 150.f);
	const float Al = FMath::Clamp(E * 4.f, 0.f, 1.f);
	const float Shake = E > 0.6f ? FMath::Sin(static_cast<float>(Now) * 60.f) * 2.f * E : 0.f;
	P.Circle(C + FVector2D(Shake, 0), 38.f, A(UI::Violet, 0.55f * Al), 7);
	P.Arc(C + FVector2D(Shake, 0), 36.f, 0.f, 1.f, A(UI::Cream, 0.3f * Al), 3.f, 8);
	P.Arc(C + FVector2D(Shake, 0), 36.f, 0.f, FMath::Max(E, 0.001f), A(FMath::Lerp(UI::Lemon, UI::Sun, E), Al), 5.f, 9);
	P.SunGlyph(C + FVector2D(Shake, 0), 13.f, E, Al, static_cast<float>(Now) * 0.8f);
	const FSlateFontInfo Warn = F(TEXT("Black"), 14, 320, P.S);
	P.Text(E > 0.6f ? TEXT("FIND SHADE!") : TEXT("TOO BRIGHT"), C + FVector2D(0.f, 50.f), Warn, A(UI::Cream, Al), 0.5f, 0.f, 0.7f, 9);
}

void SShadeOverlay::PaintLine(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	if (!G || G->GetLine().IsEmpty())
	{
		return;
	}
	const float T = static_cast<float>(Now - G->GetLineStartTime());
	const float Hold = G->GetLineHold();
	const float Al = ShadeEase::Window(T, 0.f, 0.4f) * (1.f - ShadeEase::Window(T, Hold, Hold + 0.6f));
	if (Al <= 0.01f)
	{
		return;
	}
	const FSlateFontInfo Font = F(TEXT("Hand"), 28, 0, P.S);
	const FString Str = G->GetLine().ToString();
	const FVector2D Sz = P.Measure(Str, Font);
	const float LB = ShadeEase::InOutCubic(G->GetLetterbox()) * 0.1f * P.Size.Y;
	const float Y = P.Size.Y - 92.f - FMath::Max(0.f, LB - 40.f) - (1.f - ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, 0.5f))) * 8.f;
	P.Soft(FVector2D(P.Size.X * 0.5f - Sz.X * 0.5f - 60.f, Y - 22.f), FVector2D(Sz.X + 120.f, Sz.Y + 44.f), 0.55f * Al);
	P.Text(Str, FVector2D(P.Size.X * 0.5f, Y), Font, A(UI::Cream, Al), 0.5f, 0.f, 0.6f, 12);
}

void SShadeOverlay::PaintToasts(const FPainter& P, double Now) const
{
	AShadeGameMode* G = GM();
	if (!G)
	{
		return;
	}
	const FSlateFontInfo Font = F(TEXT("Script"), 24, 0, P.S);
	float Y = UI::Margin;
	for (const FShadeToast& T : G->GetToasts())
	{
		const float Age = static_cast<float>(Now - T.StartTime);
		const float Al = ShadeEase::Window(Age, 0.f, 0.25f) * (1.f - ShadeEase::Window(Age, 3.2f, 4.f)) * HudAlpha;
		const FString Str = T.Text.ToString();
		const FVector2D Sz = P.Measure(Str, Font);
		const float W = Sz.X + 70.f;
		const FVector2D Pos(P.Size.X * 0.5f - W * 0.5f, Y - (1.f - ShadeEase::OutBack(ShadeEase::Window(Age, 0.f, 0.4f))) * 20.f);
		P.Box(Pos + FVector2D(2, 4), FVector2D(W, 46.f), A(UI::Black, 0.25f * Al), &GRound30, 2);
		P.Box(Pos, FVector2D(W, 46.f), A(UI::Sea, 0.95f * Al), &GRound30, 3);
		P.Diamond(Pos + FVector2D(24.f, 23.f), 6.f, A(UI::Lemon, Al), 4);
		P.Text(Str, Pos + FVector2D(40.f, 23.f), Font, A(UI::Cream, Al), 0.f, 0.5f, 0.3f, 5);
		Y += 56.f;
	}
}

void SShadeOverlay::PaintControls(const FPainter& P, float Alpha, const FVector2D& Origin) const
{
	const FSlateFontInfo KeyF = F(TEXT("Black"), 12, 0, P.S);
	const FSlateFontInfo Lab = F(TEXT("Bold"), 13, 140, P.S);
	struct FRow { const TCHAR* K; const TCHAR* Pad; const TCHAR* Label; };
	const FRow Rows[] = { { TEXT("WASD"), TEXT("LS"), TEXT("MOVE") }, { TEXT("MOUSE"), TEXT("RS"), TEXT("LOOK") }, { TEXT("SPACE"), TEXT("A"), TEXT("JUMP") },
		{ TEXT("SHIFT"), TEXT("L3"), TEXT("TROT") }, { TEXT("E"), TEXT("X"), TEXT("USE") } };
	float X = Origin.X;
	for (const FRow& R : Rows)
	{
		const float W = P.Key(KeyLabel(R.K, R.Pad), FVector2D(X, Origin.Y), KeyF, Alpha, 28.f);
		const FVector2D Sz = P.Text(R.Label, FVector2D(X + W + 8.f, Origin.Y + 14.f), Lab, A(UI::Cream, Alpha), 0.f, 0.5f, 0.7f);
		X += W + Sz.X + 30.f;
	}
}

void SShadeOverlay::PaintTitle(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float S = P.S;
	const float In = ShadeEase::OutCubic(ShadeEase::Window(T, 0.6f, 2.2f));
	// Soft vignette top and bottom so type reads over the sunny town.
	P.Gradient(FVector2D(0, 0), FVector2D(P.Size.X, 420.f), FLinearColor(0.03f, 0.04f, 0.08f, 0.55f * In), FLinearColor(0.03f, 0.04f, 0.08f, 0.f), Orient_Horizontal, 1);
	P.Gradient(FVector2D(0, P.Size.Y - 300.f), FVector2D(P.Size.X, 300.f), FLinearColor(0.03f, 0.04f, 0.08f, 0.f), FLinearColor(0.03f, 0.04f, 0.08f, 0.6f * In), Orient_Horizontal, 1);

	const FSlateFontInfo Logo = F(TEXT("Script"), 132, 0, S);
	const FSlateFontInfo Sub = F(TEXT("Hand"), 30, 0, S);
	const FSlateFontInfo Small = F(TEXT("Bold"), 13, 300, S);
	const FVector2D C(P.Size.X * 0.5f, 150.f - (1.f - In) * 20.f);
	// Shade's "shadow" of the logo, offset like a long late-afternoon shadow.
	P.Text(TEXT("Shade & Seek"), C + FVector2D(10.f, 12.f), Logo, A(UI::Violet, 0.55f * In), 0.5f, 0.f, 0.f, 4);
	P.Text(TEXT("Shade & Seek"), C, Logo, A(UI::Cream, In), 0.5f, 0.f, 0.f, 5);
	const FVector2D LogoSz = P.Measure(TEXT("Shade & Seek"), Logo);
	P.SunGlyph(C + FVector2D(LogoSz.X * 0.5f + 18.f, 28.f), 15.f, 0.3f, In, static_cast<float>(Now) * 0.3f);
	P.CatGlyph(C + FVector2D(-LogoSz.X * 0.5f - 26.f, 112.f), 14.f, A(UI::Violet, 0.9f * In), 5);
	P.Text(TEXT("a little shadow's long way home"), C + FVector2D(0.f, LogoSz.Y + 2.f), Sub, A(UI::Lemon, In), 0.5f, 0.f, 0.5f, 5);

	const float Pulse = 0.65f + 0.35f * FMath::Sin(static_cast<float>(Now) * 3.f);
	const float PIn = ShadeEase::Window(T, 2.0f, 2.8f);
	const FSlateFontInfo KeyF = F(TEXT("Black"), 15, 0, S);
	const FSlateFontInfo Begin = F(TEXT("Script"), 32, 0, S);
	const FString L0 = TEXT("Press");
	const FString L1 = TEXT("to begin");
	const FVector2D S0 = P.Measure(L0, Begin), S1 = P.Measure(L1, Begin);
	const float KW = 34.f;
	const float Total = S0.X + KW + S1.X + 28.f;
	const float Y = P.Size.Y - 170.f;
	float X = P.Size.X * 0.5f - Total * 0.5f;
	P.Text(L0, FVector2D(X, Y), Begin, A(UI::Cream, PIn * Pulse), 0.f, 0.5f, 0.6f, 5);
	X += S0.X + 14.f;
	P.Key(KeyLabel(TEXT("E"), TEXT("X")), FVector2D(X, Y - 17.f), KeyF, PIn, 34.f);
	X += KW + 14.f;
	P.Text(L1, FVector2D(X, Y), Begin, A(UI::Cream, PIn * Pulse), 0.f, 0.5f, 0.6f, 5);
	PaintControls(P, PIn * 0.85f, FVector2D(P.Size.X * 0.5f - 330.f, P.Size.Y - 110.f));
	P.Text(TEXT("VELLARA  ·  THE COAST  ·  LATE AFTERNOON"), FVector2D(P.Size.X * 0.5f, P.Size.Y - 52.f), Small, A(UI::Cream, 0.7f * PIn), 0.5f, 0.f, 0.6f, 5);
}

void SShadeOverlay::PaintMenuItems(const FPainter& P, double Now, const FVector2D& Center, float Alpha) const
{
	AShadeHUD* H = Owner.Get();
	const FSlateFontInfo Item = F(TEXT("Script"), 30, 0, P.S);
	const int32 N = H->GetMenuItemCount();
	for (int32 i = 0; i < N; ++i)
	{
		const FString Str = H->GetMenuItemLabel(i).ToString();
		const FVector2D C = Center + FVector2D(0.f, i * 56.f);
		const bool bSel = i == H->Selected;
		const FVector2D Sz = P.Measure(Str, Item);
		const FVector2D Pos(C.X - 150.f, C.Y - 24.f);
		if (bSel)
		{
			P.Box(Pos, FVector2D(300.f, 48.f), A(UI::Terracotta, 0.95f * Alpha), &GRound30, 6);
			P.Diamond(FVector2D(Pos.X + 26.f, C.Y), 6.f, A(UI::Lemon, Alpha), 7);
			P.Diamond(FVector2D(Pos.X + 274.f, C.Y), 6.f, A(UI::Lemon, Alpha), 7);
		}
		P.Text(Str, C, Item, A(bSel ? UI::Cream : UI::Ink, Alpha), 0.5f, 0.5f, 0.f, 8);
		ItemRects.Add(FBox2D(Pos, Pos + FVector2D(300.f, 48.f)));
	}
}

void SShadeOverlay::PaintPause(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float Al = ShadeEase::OutCubic(ShadeEase::Window(T, 0.f, 0.25f));
	P.Box(FVector2D(0, 0), P.Size, FLinearColor(0.03f, 0.04f, 0.08f, 0.45f * Al), &GSolid, 5);
	const FVector2D Sz(420.f, 330.f);
	const FVector2D Pos = P.Size * 0.5f - Sz * 0.5f;
	P.Tile(Pos, Sz, Al);
	const FSlateFontInfo Head = F(TEXT("Script"), 46, 0, P.S);
	P.Text(TEXT("Siesta"), FVector2D(P.Size.X * 0.5f, Pos.Y + 36.f), Head, A(UI::Ink, Al), 0.5f, 0.f, 0.f, 6);
	PaintMenuItems(P, Now, FVector2D(P.Size.X * 0.5f, Pos.Y + 160.f), Al);
}

void SShadeOverlay::PaintEnd(const FPainter& P, double Now) const
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	AShadeCharacter* K = Shade();
	const float T = static_cast<float>(Now - H->GetMenuOpenTime());
	const float S = P.S;
	const float In = ShadeEase::OutCubic(ShadeEase::Window(T, 0.3f, 1.8f));
	P.Gradient(FVector2D(0, P.Size.Y * 0.35f), FVector2D(P.Size.X, P.Size.Y * 0.65f), FLinearColor(0.05f, 0.03f, 0.08f, 0.f), FLinearColor(0.05f, 0.03f, 0.08f, 0.7f * In), Orient_Horizontal, 1);
	const FSlateFontInfo Logo = F(TEXT("Script"), 110, 0, S);
	const FSlateFontInfo Sub = F(TEXT("Hand"), 28, 0, S);
	const FSlateFontInfo StatN = F(TEXT("Script"), 36, 0, S);
	const FSlateFontInfo StatL = F(TEXT("Black"), 12, 260, S);
	const FVector2D C(P.Size.X * 0.5f, P.Size.Y * 0.44f - (1.f - In) * 20.f);
	P.Text(TEXT("Reunited"), C + FVector2D(9.f, 11.f), Logo, A(UI::Violet, 0.55f * In), 0.5f, 0.5f, 0.f, 4);
	P.Text(TEXT("Reunited"), C, Logo, A(UI::Cream, In), 0.5f, 0.5f, 0.f, 5);
	P.Text(TEXT("Biscotto has his shadow back - just in time for supper."), C + FVector2D(0.f, 78.f), Sub, A(UI::Lemon, In), 0.5f, 0.5f, 0.5f, 5);

	const float SIn = ShadeEase::Window(T, 1.2f, 2.0f);
	const FString Stats[3][2] = {
		{ G ? FormatTime(G->GetRunTime()) : TEXT("-"), TEXT("TIME") },
		{ FString::FromInt(G ? G->GetPuzzlesSolved() : 0) + TEXT("/3"), TEXT("SHADE MADE") },
		{ FString::FromInt(K ? K->GetEvaporations() : 0), TEXT("EVAPORATIONS") } };
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector2D SC(P.Size.X * 0.5f + (i - 1) * 220.f, C.Y + 160.f);
		P.Text(Stats[i][0], SC, StatN, A(UI::Cream, SIn), 0.5f, 0.5f, 0.5f, 5);
		P.Text(Stats[i][1], SC + FVector2D(0.f, 34.f), StatL, A(UI::Lemon, SIn), 0.5f, 0.5f, 0.5f, 5);
		if (i > 0) { P.Diamond(SC - FVector2D(110.f, -4.f), 4.f, A(UI::Terracotta, SIn), 5); }
	}
	PaintMenuItems(P, Now, FVector2D(P.Size.X * 0.5f, C.Y + 270.f), ShadeEase::Window(T, 1.8f, 2.4f));
}

FReply SShadeOverlay::OnKeyDown(const FGeometry& Geo, const FKeyEvent& Key)
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	if (!H || !G || (PC() && PC()->IsAutoplaying()))
	{
		return FReply::Unhandled();
	}
	const FKey K = Key.GetKey();
	if (H->GetMenu() == EShadeMenu::Title)
	{
		if (K == EKeys::E || K == EKeys::Enter || K == EKeys::SpaceBar || K == EKeys::Gamepad_FaceButton_Left || K == EKeys::Gamepad_FaceButton_Bottom)
		{
			G->StartGame();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	const int32 N = H->GetMenuItemCount();
	if (N > 0)
	{
		if (K == EKeys::Up || K == EKeys::W || K == EKeys::Gamepad_DPad_Up)
		{
			H->Selected = (H->Selected + N - 1) % N;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.5f);
			return FReply::Handled();
		}
		if (K == EKeys::Down || K == EKeys::S || K == EKeys::Gamepad_DPad_Down)
		{
			H->Selected = (H->Selected + 1) % N;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.5f);
			return FReply::Handled();
		}
		if (K == EKeys::Enter || K == EKeys::E || K == EKeys::SpaceBar || K == EKeys::Gamepad_FaceButton_Bottom)
		{
			H->ActivateMenuItem(H->Selected);
			return FReply::Handled();
		}
	}
	if ((K == EKeys::Escape || K == EKeys::P || K == EKeys::Gamepad_Special_Right) && H->GetMenu() == EShadeMenu::Pause)
	{
		if (AShadePlayerController* C = PC()) { C->ClosePauseMenu(); }
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply SShadeOverlay::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Mouse)
{
	AShadeHUD* H = Owner.Get();
	if (!H)
	{
		return FReply::Unhandled();
	}
	const FVector2D Local = Geo.AbsoluteToLocal(Mouse.GetScreenSpacePosition()) / LastS;
	for (int32 i = 0; i < ItemRects.Num(); ++i)
	{
		if (ItemRects[i].IsInside(Local) && H->Selected != i)
		{
			H->Selected = i;
			ShadeAssets::Play2D(H, TEXT("SFX_UI_Move"), 0.4f);
		}
	}
	return FReply::Unhandled();
}

FReply SShadeOverlay::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Mouse)
{
	AShadeHUD* H = Owner.Get();
	AShadeGameMode* G = GM();
	if (!H || !G)
	{
		return FReply::Unhandled();
	}
	if (H->GetMenu() == EShadeMenu::Title)
	{
		G->StartGame();
		return FReply::Handled();
	}
	const FVector2D Local = Geo.AbsoluteToLocal(Mouse.GetScreenSpacePosition()) / LastS;
	for (int32 i = 0; i < ItemRects.Num(); ++i)
	{
		if (ItemRects[i].IsInside(Local))
		{
			H->Selected = i;
			H->ActivateMenuItem(i);
			return FReply::Handled();
		}
	}
	return FReply::Handled();
}

// =====================================================================================================================
// AShadeHUD
// =====================================================================================================================

void AShadeHUD::BeginPlay()
{
	Super::BeginPlay();
	const FString Dir = FPaths::ProjectContentDir() / TEXT("Shade/UI/Fonts/");
	struct FFace { const TCHAR* Name; const TCHAR* File; };
	const FFace Faces[] = {
		{ TEXT("Script"), TEXT("Lobster-Regular.ttf") }, { TEXT("Hand"), TEXT("Kalam-Regular.ttf") },
		{ TEXT("Body"), TEXT("Lato-Regular.ttf") }, { TEXT("Bold"), TEXT("Lato-Bold.ttf") },
		{ TEXT("Black"), TEXT("Lato-Black.ttf") }, { TEXT("Italic"), TEXT("Lato-Italic.ttf") },
	};
	for (const FFace& Face : Faces)
	{
		const FString Path = Dir + Face.File;
		if (IFileManager::Get().FileExists(*Path))
		{
			TSharedPtr<FStandaloneCompositeFont> C = MakeShared<FStandaloneCompositeFont>();
			C->DefaultTypeface.AppendFont(TEXT("Regular"), Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
			Fonts.Add(Face.Name, C);
		}
		else
		{
			UE_LOG(LogShade, Warning, TEXT("UI font missing: %s"), *Path);
		}
	}

	TWeakObjectPtr<AShadeHUD> Self(this);
	SAssignNew(Root, SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBackgroundBlur)
			.bApplyAlphaToBlur(true)
			.BlurStrength_Lambda([Self]()
			{
				if (!Self.IsValid()) { return 0.f; }
				const double T = FPlatformTime::Seconds() - Self->GetMenuOpenTime();
				return Self->GetMenu() == EShadeMenu::Pause ? 10.f * ShadeEase::OutCubic(static_cast<float>(T / 0.3)) : 0.f;
			})
			.Visibility_Lambda([Self]()
			{
				return (Self.IsValid() && Self->GetMenu() == EShadeMenu::Pause) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
		]
		+ SOverlay::Slot()
		[
			SAssignNew(Overlay, SShadeOverlay)
			.Owner(Self)
			.Visibility_Lambda([Self]()
			{
				return (Self.IsValid() && Self->GetMenu() != EShadeMenu::None) ? EVisibility::Visible : EVisibility::HitTestInvisible;
			})
		];
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 50);
	}
	if (AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>())
	{
		if (AShadePlayerController* P = Cast<AShadePlayerController>(GetOwningPlayerController()))
		{
			P->OnPhaseChanged(GM->GetPhase());
		}
	}
}

void AShadeHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport && Root.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	}
	Root.Reset();
	Overlay.Reset();
	Super::EndPlay(Reason);
}

TSharedPtr<SWidget> AShadeHUD::GetFocusWidget() const
{
	return Overlay;
}

FSlateFontInfo AShadeHUD::Font(FName Face, float Size, int32 LetterSpacing) const
{
	const float Px = FMath::Max(1.f, Size);
	FSlateFontInfo Info;
	if (const TSharedPtr<FStandaloneCompositeFont>* C = Fonts.Find(Face))
	{
		Info = FSlateFontInfo(StaticCastSharedPtr<const FCompositeFont>(*C), Px, TEXT("Regular"));
	}
	else
	{
		Info = FCoreStyle::GetDefaultFontStyle(Face == TEXT("Black") || Face == TEXT("Bold") ? TEXT("Bold") : TEXT("Regular"), FMath::RoundToInt(Px));
	}
	Info.LetterSpacing = LetterSpacing;
	return Info;
}

void AShadeHUD::ShowMenu(EShadeMenu NewMenu)
{
	if (Menu == NewMenu)
	{
		return;
	}
	Menu = NewMenu;
	MenuOpenTime = FPlatformTime::Seconds();
	Selected = 0;
	if (Menu != EShadeMenu::None && Overlay.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(Overlay, EFocusCause::SetDirectly);
	}
}

int32 AShadeHUD::GetMenuItemCount() const
{
	switch (Menu)
	{
	case EShadeMenu::Pause: return 3;
	case EShadeMenu::End: return 2;
	default: return 0;
	}
}

FText AShadeHUD::GetMenuItemLabel(int32 Index) const
{
	if (Menu == EShadeMenu::Pause)
	{
		const FText L[3] = { LOCTEXT("Resume", "Back to the sun"), LOCTEXT("Restart", "Start over"), LOCTEXT("Quit", "Close the shutters") };
		return L[FMath::Clamp(Index, 0, 2)];
	}
	const FText L[2] = { LOCTEXT("Again", "Wander again"), LOCTEXT("QuitEnd", "Close the shutters") };
	return L[FMath::Clamp(Index, 0, 1)];
}

void AShadeHUD::ActivateMenuItem(int32 Index)
{
	AShadeGameMode* GM = GetWorld()->GetAuthGameMode<AShadeGameMode>();
	AShadePlayerController* P = Cast<AShadePlayerController>(GetOwningPlayerController());
	if (!GM || !P)
	{
		return;
	}
	ShadeAssets::Play2D(this, TEXT("SFX_UI_Select"), 0.7f);
	if (Menu == EShadeMenu::Pause)
	{
		if (Index == 0) { P->ClosePauseMenu(); }
		else if (Index == 1) { P->ClosePauseMenu(); GM->RestartRun(); }
		else { GM->QuitGame(); }
	}
	else if (Menu == EShadeMenu::End)
	{
		if (Index == 0) { GM->RestartRun(); }
		else { GM->QuitGame(); }
	}
}

#undef LOCTEXT_NAMESPACE
