import csv, sys
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt
rows=list(csv.DictReader(open("sim.csv")))
t=[float(r["t"]) for r in rows]
cen=[l.strip().split(",") for l in open("cenas.csv")]
nomes={1:"sem controle",2:"conecta",3:"drift",4:"frente 50%",5:"meio analog.",6:"solta",7:"R1 x6",8:"D-pad 100%",
       9:"vira direita",10:"X stop",11:"BT cai",12:"reconecta (ré)",13:"2º controle",14:"clone sem dados",15:"gimbal",
       16:"L1 → 0%",17:"diagonal",18:"telnet",19:"pagina",20:"reinicio",21:"grava flash",22:"para na hora"}
fig,ax=plt.subplots(3,1,figsize=(14,9),sharex=True,gridspec_kw={"height_ratios":[3,1,1.4]})
sent=[int(r["sentido"]) for r in rows]; sps=[float(r["passos_s"]) for r in rows]
cores={0:"#999",1:"#2563eb",-1:"#dc2626",2:"#16a34a"}
for s,lab in [(1,"frente"),(-1,"ré"),(2,"gira (tank)"),(0,"parado / freando")]:
    ax[0].scatter([x for x,v in zip(t,sent) if v==s],[y for y,v in zip(sps,sent) if v==s],s=4,c=cores[s],label=lab)
ax[0].set_ylabel("passos/s (cada roda)"); ax[0].legend(loc="upper left",markerscale=4)
ax[0].set_title("Simulação dois_nema17_esp32v2_ps4 — controle PS4 roteirizado (sem hardware)")
ax[1].plot(t,[int(r["drivers"]) for r in rows],c="#7c3aed",drawstyle="steps-post"); ax[1].set_ylabel("drivers\nligados"); ax[1].set_yticks([0,1])
ax[1].plot(t,[int(r["limite"])/100 for r in rows],c="#ea580c",ls="--",label="limite L1/R1 (0–100%)"); ax[1].legend(loc="upper left")
ax[2].plot(t,[float(r["pan"]) for r in rows],label="pan"); ax[2].plot(t,[float(r["tilt"]) for r in rows],label="tilt")
ax[2].set_ylabel("gimbal (graus)"); ax[2].set_xlabel("tempo simulado (s)"); ax[2].legend(loc="upper left")
for a in ax:
    for tc,n in cen: a.axvline(float(tc),c="#ccc",lw=.7,zorder=0)
for tc,n in cen: ax[0].text(float(tc)+.05,3500,f"{n}) {nomes[int(n)]}",rotation=90,va="top",fontsize=7.5)
ax[0].set_ylim(0,3600)
plt.tight_layout(); plt.savefig(sys.argv[1] if len(sys.argv)>1 else "sim.png",dpi=110)
