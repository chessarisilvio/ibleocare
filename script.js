const tempValue = document.getElementById("tempValue");
const soilValue = document.getElementById("soilValue");
const etaValue = document.getElementById("etaValue");
const soilState = document.getElementById("soilState");
const statusLabel = document.getElementById("statusLabel");
const year = document.getElementById("year");
const menuToggle = document.getElementById("menuToggle");
const navLinks = document.getElementById("navLinks");

year.textContent = new Date().getFullYear();

function getSoilLabel(pct) {
  if (pct < 25) return "SECCO";
  if (pct < 50) return "ASCIUTTO";
  if (pct < 75) return "UMIDO";
  return "SATURO";
}

function formatTempo(ore) {
  if (ore <= 0) return "annaffia ora";
  if (ore < 1) return `tra ${Math.round(ore * 60)} min`;

  if (ore < 48) {
    const h = Math.floor(ore);
    const m = Math.round((ore - h) * 60);
    return m > 0 ? `tra ${h}h ${m}m` : `tra ${h} ore`;
  }

  const g = Math.floor(ore / 24);
  const h = Math.floor(ore % 24);
  return h > 0 ? `tra ${g}g ${h}h` : `tra ${g} giorni`;
}

// Simulazione ispirata alla logica del vostro firmware.
// In seguito questi valori potranno arrivare dal dispositivo reale.
function updateSimulation() {
  const temp = (23 + Math.random() * 5.5).toFixed(1);
  const soil = Math.floor(20 + Math.random() * 65);

  let oreMancanti = 0;
  if (soil >= 70) {
    oreMancanti = 30 + Math.random() * 30;
  } else if (soil >= 50) {
    oreMancanti = 14 + Math.random() * 18;
  } else if (soil >= 33) {
    oreMancanti = 5 + Math.random() * 10;
  } else {
    oreMancanti = Math.random() * 1.5;
  }

  tempValue.textContent = temp;
  soilValue.textContent = soil;
  soilState.textContent = getSoilLabel(soil);
  etaValue.textContent = formatTempo(oreMancanti);

  statusLabel.classList.remove("ok", "warn", "dry");

  if (soil >= 66) {
    statusLabel.textContent = "ottimale";
    statusLabel.classList.add("ok");
  } else if (soil >= 33) {
    statusLabel.textContent = "attenzione";
    statusLabel.classList.add("warn");
  } else {
    statusLabel.textContent = "secco";
    statusLabel.classList.add("dry");
  }
}

updateSimulation();
setInterval(updateSimulation, 2600);

const observer = new IntersectionObserver(
  (entries) => {
    entries.forEach((entry) => {
      if (entry.isIntersecting) {
        entry.target.classList.add("visible");
      }
    });
  },
  { threshold: 0.14 }
);

document.querySelectorAll(".reveal").forEach((el) => observer.observe(el));

menuToggle?.addEventListener("click", () => {
  navLinks.classList.toggle("open");
});

document.querySelectorAll('.nav-links a').forEach((link) => {
  link.addEventListener("click", () => {
    navLinks.classList.remove("open");
  });
});
