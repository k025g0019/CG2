/* ================================================================
   CG2 Docs UI Controller
   - 検索
   - コードコピー
   - テーマ切り替え
   - モバイルナビ
   ================================================================ */

(() => {
	"use strict";

	const body = document.body;
	const searchInput = document.getElementById("apiSearch");
	const clearSearchButton = document.getElementById("clearSearch");
	const visibleCount = document.getElementById("visibleCount");
	const menuToggle = document.getElementById("menuToggle");
	const themeToggle = document.getElementById("themeToggle");
	const backToTop = document.getElementById("backToTop");
	const navItems = Array.from(document.querySelectorAll(".nav-item"));
	const navGroups = Array.from(document.querySelectorAll(".nav-group"));
	const apiCards = Array.from(document.querySelectorAll(".searchable-section"));

	const savedTheme = localStorage.getItem("cg2-docs-theme");

	if (savedTheme === "dark") {
		body.classList.add("dark");
	}

	themeToggle?.addEventListener("click", () => {
		body.classList.toggle("dark");
		localStorage.setItem("cg2-docs-theme", body.classList.contains("dark") ? "dark" : "light");
	});

	menuToggle?.addEventListener("click", () => {
		body.classList.toggle("nav-open");
	});

	navItems.forEach((item) => {
		item.addEventListener("click", () => {
			body.classList.remove("nav-open");
		});
	});

	const normalize = (value) => value.trim().toLowerCase();

	const filterDocs = () => {
		const query = normalize(searchInput?.value ?? "");
		let matchCount = 0;
		const visibleCardIds = new Set();

		apiCards.forEach((card) => {
			const keywords = normalize(card.textContent ?? "");
			const isMatch = !query || keywords.includes(query);
			card.classList.toggle("hidden-by-search", !isMatch);

			if (isMatch) {
				matchCount += 1;
				visibleCardIds.add(card.id);
			}
		});

		navItems.forEach((item) => {
			const keywords = normalize(item.textContent ?? "");
			const targetId = (item.getAttribute("href") ?? "").replace("#", "");
			const isTargetVisible = visibleCardIds.has(targetId);
			const isMatch = !query || isTargetVisible || keywords.includes(query);
			item.classList.toggle("hidden-by-search", !isMatch);
		});

		navGroups.forEach((group) => {
			const visibleItems = group.querySelectorAll(".nav-item:not(.hidden-by-search)");
			group.classList.toggle("hidden-by-search", visibleItems.length === 0);
			group.open = query !== "" && visibleItems.length > 0 ? true : group.open;
		});

		if (visibleCount) {
			visibleCount.textContent = String(matchCount);
		}
	};

	searchInput?.addEventListener("input", filterDocs);

	clearSearchButton?.addEventListener("click", () => {
		if (!searchInput) {
			return;
		}

		searchInput.value = "";
		searchInput.focus();
		filterDocs();
	});

	window.addEventListener("keydown", (event) => {
		if (event.key !== "/" || !searchInput) {
			return;
		}

		const activeTagName = document.activeElement?.tagName ?? "";

		if (activeTagName === "INPUT" || activeTagName === "TEXTAREA") {
			return;
		}

		event.preventDefault();
		searchInput.focus();
	});

	document.querySelectorAll(".copy-button").forEach((button) => {
		button.addEventListener("click", async () => {
			const codeBlock = button.closest(".code-block")?.querySelector("code");
			const codeText = codeBlock?.textContent ?? "";

			try {
				await navigator.clipboard.writeText(codeText);
				button.textContent = "Copied";
				button.classList.add("copied");

				setTimeout(() => {
					button.textContent = "Copy";
					button.classList.remove("copied");
				}, 900);
			} catch {
				button.textContent = "Failed";
				setTimeout(() => {
					button.textContent = "Copy";
				}, 900);
			}
		});
	});

	const navByHash = new Map(navItems.map((item) => [item.getAttribute("href"), item]));

	if (apiCards.length > 0 && navItems.length > 0) {
		const observer = new IntersectionObserver(
			(entries) => {
				const visibleEntry = entries.find((entry) => entry.isIntersecting);

				if (!visibleEntry) {
					return;
				}

				const hash = `#${visibleEntry.target.id}`;
				navItems.forEach((item) => item.classList.remove("active"));
				navByHash.get(hash)?.classList.add("active");
			},
			{
				rootMargin: "-18% 0px -72% 0px",
				threshold: 0.01,
			}
		);

		apiCards.forEach((card) => observer.observe(card));
	}

	window.addEventListener("scroll", () => {
		backToTop?.classList.toggle("visible", window.scrollY > 900);
	});

	backToTop?.addEventListener("click", () => {
		window.scrollTo({ top: 0, behavior: "smooth" });
	});

	filterDocs();
})();
