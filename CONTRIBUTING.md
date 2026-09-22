# Contributing

Pull requests are welcome.

1. **Fork** this repository (the button at the top right on GitHub). You
   cannot push branches here directly; your fork is where your branches live.
2. **Branch** in your fork from `main`, and commit your work there.
3. **Build and try it** on your platform before opening the pull request
   (see [README.md](README.md)). For rendering changes, a before/after
   screenshot helps a lot - for example
   `Mriya.scr --window 1600x900 --dump out.png --frames 90 --weather sunset --camera window`.
4. **Open a pull request** against `main` here. Every pull request is reviewed
   by the maintainer, who approves and merges it.

Keep changes focused: one fix or feature per pull request. Match the style of
the surrounding code - C11, four-space indents, comments that explain *why*.
Settings go in the single table in `src/settings.h`; new shaders must be added
to `MR_SHADERS` in `CMakeLists.txt`; new weather scenarios and cameras are
appended to the ends of their lists in `src/settings.h`, never inserted, so
saved settings keep meaning the same thing.

Linux testing is especially welcome: that port is written but has not been
run on a real Linux machine yet.
