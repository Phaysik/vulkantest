from cgitb import html

extensions = [
    "breathe",
    "sphinx_copybutton",
    "sphinx_last_updated_by_git",
    "notfound.extension",
]

master_doc = "rst/index"
html_title = "{project_name}"
html_theme = "sphinx_book_theme"
html_static_path = ["docs/_static/"]

html_css_files = ["css/custom.css"]

html_theme_options = {
    "repository_url": "{github_repository_url}",
    "use_repository_button": True,
    "home_page_in_toc": True,
}

copybutton_prompt_text = "Copy to clipboard"

# Breathe configuration
breathe_default_project = "documentation"
breathe_default_members = ("members", "protected-members", "private-members")
