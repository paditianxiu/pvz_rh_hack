package main

import (
	"embed"
	_ "embed"
	"log"
	"time"

	"github.com/wailsapp/wails/v3/pkg/application"
)

// Wails uses Go's `embed` package to embed the frontend files into the binary.
// Any files in the frontend/dist folder will be embedded into the binary and
// made available to the frontend.
// See https://pkg.go.dev/embed for more information.

//go:embed all:frontend/dist
var assets embed.FS

func init() {
	application.RegisterEvent[string]("time")
}

func main() {

	app := application.New(application.Options{
		Name:        "pvz_rh_hack",
		Description: "A demo of using raw HTML & CSS",
		Services: []application.Service{
			application.NewService(&ProcessService{}),
		},
		Assets: application.AssetOptions{
			Handler: application.AssetFileServerFS(assets),
		},
		Mac: application.MacOptions{
			ApplicationShouldTerminateAfterLastWindowClosed: true,
		},
	})

	app.Window.NewWithOptions(application.WebviewWindowOptions{
		Name:  "main",
		Title: "PVZ融合版辅助",
		Mac: application.MacWindow{
			InvisibleTitleBarHeight: 50,
			Backdrop:                application.MacBackdropTranslucent,
			TitleBar:                application.MacTitleBarHiddenInset,
		},
		BackgroundColour: application.NewRGB(255, 255, 255),
		URL:              "/",
	})

	app.Window.NewWithOptions(application.WebviewWindowOptions{
		Name:              "zombie-overlay",
		Title:             "ZombieOverlay",
		Width:             1280,
		Height:            720,
		Frameless:         true,
		DisableResize:     true,
		AlwaysOnTop:       true,
		IgnoreMouseEvents: true,
		BackgroundType:    application.BackgroundTypeTransparent,
		Hidden:            true,
		Windows: application.WindowsWindow{
			HiddenOnTaskbar: true,
		},
		URL: "/#/overlay",
	})

	go func() {
		for {
			now := time.Now().Format(time.RFC1123)
			app.Event.Emit("time", now)
			time.Sleep(time.Second)
		}
	}()

	err := app.Run()

	if err != nil {
		log.Fatal(err)
	}
}
