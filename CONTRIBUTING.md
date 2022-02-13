# Contributing
Development should loosely follow the [GitFlow](http://datasift.github.io/gitflow/IntroducingGitFlow.html):
- Develop changes branching from the `development` branch
- Create a `release` branch to merge into the `main` branch
- The `main` branch should always contain a functioning version; testing of new features etc should happen in `development` or other branches
- Try to merge any desired changes into `development` prior to creating a release branch. If there are any changes in the release branch remember to merge then back into `development` as well as `main`. Note that merging counts as a commit so GitHub might say you're a commit behind when in fact your code is identical.
