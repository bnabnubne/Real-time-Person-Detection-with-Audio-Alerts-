import streamlit as st
import pandas as pd
import numpy as np
import time
from datetime import datetime
import cv2
import base64

# --- 1. CONFIG & SOUND DATA ---
st.set_page_config(
    page_title="Monitor Pro",
    layout="wide",
    initial_sidebar_state="collapsed"
)

# Mã hóa tiếng Bíp
BEEP_B64 = "data:audio/mp3;base64,SUQzBAAAAAABAFRYWFgAAAASAAADbWFqb3JfYnJhbmQAbXA0MgBUWFhYAAAAEQAAA21pbm9yX3ZlcnNpb24AMABUWFhYAAAAHAAAA2NvbXBhdGlibGVfYnJhbmRzAGlzb21tcDQyAFRTU0UAAAAOAAADTGF2ZjU3LjU2LjEwMAAAAAAAAAAAAAAA//uQZAAAAAAAALAAAAABAAABAAAAAAAALAAAAABAAABAAAAAAAAAAAAAAP/7kGQAAAAAAAAsAAAAAEAAAEAAAAAAAAsAAAAAEAAAEAAAAAAAAAAAAAD/+5BkAAAAAAAAKwAAAAAQAAAQAAAAAAAACwAAAAAQAAAQAAAAAAAAAAAAAP/7kGQAAAAAAAAsAAAAAEAAAEAAAAAAAAsAAAAAEAAAEAAAAAAAAAAAAAD/+5BkAAAAAAAAKwAAAAAQAAAQAAAAAAAACwAAAAAQAAAQAAAAAAAAAAAAAP/7kGQAAAAAAAAsAAAAAEAAAEAAAAAAAAsAAAAAEAAAEAAAAAAAAAAAAAD/+5BkAAAAAAAAKwAAAAAQAAAQAAAAAAAACwAAAAAQAAAQAAAAAAAAAAAAAP/7kGQAAAAAAAAsAAAAAEAAAEAAAAAAAAsAAAAAEAAAEAAAAAAAAAAAAAD/"

THEME_COLOR = "#FF4D88"
BG_COLOR = "#000000"
CARD_BG = "#121212"

# --- 2. CSS STYLING ---
st.markdown(f"""
<style>
    .stApp {{ background-color: {BG_COLOR}; color: {THEME_COLOR}; }}
    
    div.stButton > button {{
        width: 100%; border-radius: 8px; border: 1px solid #333;
        background-color: {CARD_BG}; color: {THEME_COLOR};
        font-family: sans-serif; font-weight: 800; text-transform: uppercase;
        transition: all 0.3s ease; padding: 12px;
    }}
    div.stButton > button:hover {{
        border-color: {THEME_COLOR}; background-color: #1E000A;
        box-shadow: 0 0 10px rgba(255, 77, 136, 0.2); color: white;
    }}
    
    .stDataFrame {{ border: 1px solid #333; border-radius: 8px; font-family: monospace; }}
    h1, h2, h3, h4 {{ 
        color: {THEME_COLOR} !important; font-family: sans-serif; 
        font-weight: 900 !important; letter-spacing: 1px; text-decoration: none !important;
    }}
    
    .stTabs [data-baseweb="tab-list"] {{ gap: 10px; border-bottom: none !important; }}
    .stTabs [data-baseweb="tab"] {{
        height: 50px; background-color: {CARD_BG}; border-radius: 5px;
        color: #888; font-weight: bold; border: none !important;
    }}
    .stTabs [aria-selected="true"] {{
        background-color: {THEME_COLOR} !important; color: white !important;
    }}
    div[data-baseweb="tab-highlight"] {{ background-color: transparent !important; }}
    #MainMenu, footer, header {{visibility: hidden;}}
</style>
""", unsafe_allow_html=True)

# --- 3. STATE MANAGEMENT ---
# Dùng biến 'system_state' có 3 giá trị: STOPPED, RUNNING, PAUSED
if 'system_state' not in st.session_state: st.session_state['system_state'] = 'STOPPED'
if 'log_history' not in st.session_state: st.session_state['log_history'] = []
if 'last_update' not in st.session_state: st.session_state['last_update'] = 0

# --- 4. LAYOUT ---
st.markdown(f"<h2 style='text-align: center; margin-bottom: 20px;'>REAL-TIME OBJECT DETECTION</h2>", unsafe_allow_html=True)

col_left, col_right = st.columns([2, 1.2], gap="large")

# --- HELPER FUNCTIONS ---
def get_mock_data():
    objects = ["Person", "Person", "Student", "Staff", "Person", "Unknown"]
    conf = np.random.uniform(0.70, 0.98)
    obj = np.random.choice(objects)
    is_alert = True if obj == "Unknown" else False
    return obj, conf, is_alert

def play_sound():
    sound_html = f'<audio autoplay="true" src="{BEEP_B64}"></audio>'
    st.markdown(sound_html, unsafe_allow_html=True)

def render_analytics(chart_place, log_place, status_place):
    df = pd.DataFrame(st.session_state['log_history'])
    if not df.empty:
        chart_data = df[['TIME', 'CONF']].iloc[::-1].set_index('TIME')
        with chart_place:
            st.line_chart(chart_data, color=THEME_COLOR, height=220)
        
        df_display = df.copy()
        df_display['CONF'] = df_display['CONF'].apply(lambda x: f"{x:.0%}")
        log_place.dataframe(df_display, height=350, use_container_width=True, hide_index=True)
        
        latest = st.session_state['log_history'][0]
        if latest['STATUS'] == "ALERT":
            status_place.markdown(f"<div style='background-color: #2A0000; padding: 15px; border-radius: 8px; border-left: 5px solid #FF0000;'><h4 style='color: #FF4444 !important; margin: 0;'> WARNING: {latest['OBJECT']}</h4></div>", unsafe_allow_html=True)
        else:
            status_place.markdown(f"<div style='background-color: #121212; padding: 15px; border-radius: 8px; border-left: 5px solid {THEME_COLOR};'><h4 style='color: {THEME_COLOR} !important; margin: 0;'> SYSTEM NORMAL</h4></div>", unsafe_allow_html=True)

# --- 5. UI COMPONENTS ---
with col_left:
    st.write("")
    cam_placeholder = st.empty()
    st.write("")
    
    b1, b2, b3 = st.columns(3)
    
    with b1:
        # START NEW: Chuyển sang RUNNING, Xóa log cũ
        if st.button("START NEW"):
            st.session_state['system_state'] = 'RUNNING'
            st.session_state['log_history'] = [] 
            st.rerun()

    with b2:
        # Logic nút giữa: Pause/Resume tùy trạng thái
        if st.session_state['system_state'] == 'RUNNING':
            if st.button("⏸ PAUSE"):
                st.session_state['system_state'] = 'PAUSED'
                st.rerun()
        else:
            # Hiện nút Resume khi đang PAUSED hoặc STOPPED
            if st.button("▶ RESUME"):
                st.session_state['system_state'] = 'RUNNING'
                st.rerun()

    with b3:
        # RESET: Chuyển về STOPPED
        if st.button("RESET LOGS"):
            st.session_state['log_history'] = []
            st.session_state['system_state'] = 'STOPPED'
            st.rerun()

with col_right:
    tab1, tab2 = st.tabs(["ANALYTICS", "DATA LOGS"])
    with tab1:
        st.write("")
        chart_placeholder = st.empty()
        st.write("")
        status_placeholder = st.empty()
    with tab2:
        log_placeholder = st.empty()

# --- 6. MAIN LOGIC (Sửa lại theo trạng thái 3 mức) ---

if st.session_state['system_state'] == 'RUNNING':
    # KHI ĐANG CHẠY
    cap = cv2.VideoCapture(0)
    
    if not cap.isOpened():
        cam_placeholder.error("NO SIGNAL")
    else:
        while st.session_state['system_state'] == 'RUNNING':
            ret, frame = cap.read()
            if not ret: break

            now = time.time()
            current_sec = int(now)
            timestamp_str = datetime.now().strftime("%H:%M:%S")

            if current_sec != st.session_state['last_update']:
                st.session_state['last_update'] = current_sec
                obj, conf, is_alert = get_mock_data()
                if is_alert: play_sound()

                new_log = {"TIME": timestamp_str, "OBJECT": obj, "CONF": conf, "STATUS": "ALERT" if is_alert else "OK"}
                st.session_state['log_history'].insert(0, new_log)
                if len(st.session_state['log_history']) > 50: st.session_state['log_history'].pop()

                render_analytics(chart_placeholder, log_placeholder, status_placeholder)

            # UI Camera
            if st.session_state['log_history']:
                latest = st.session_state['log_history'][0]
                obj_d, conf_d = latest['OBJECT'], latest['CONF']
            else:
                obj_d, conf_d = "INIT", 0.0

            h, w, _ = frame.shape
            color_bgr = (136, 77, 255) 
            thick = 2; L = 30
            for x, y in [(w/4, h/4), (w*3/4, h/4), (w/4, h*3/4), (w*3/4, h*3/4)]:
                sgn_x = 1 if x < w/2 else -1; sgn_y = 1 if y < h/2 else -1
                cv2.line(frame, (int(x), int(y)), (int(x + sgn_x*L), int(y)), color_bgr, thick)
                cv2.line(frame, (int(x), int(y)), (int(x), int(y + sgn_y*L)), color_bgr, thick)

            overlay = frame.copy()
            cv2.rectangle(overlay, (int(w/4), int(h/4)-30), (int(w/4)+250, int(h/4)-5), (0,0,0), -1)
            frame = cv2.addWeighted(overlay, 0.7, frame, 0.3, 0)
            text = f"{obj_d} | {conf_d:.0%}"
            cv2.putText(frame, text, (int(w/4)+10, int(h/4)-12), cv2.FONT_HERSHEY_SIMPLEX, 0.6, color_bgr, 2)

            cam_placeholder.image(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB), channels="RGB", use_container_width=True)
            time.sleep(0.03)

        cap.release()

elif st.session_state['system_state'] == 'PAUSED':
    # KHI PAUSED (Người dùng bấm Pause)
    cam_placeholder.markdown(f"""
        <div style='background-color: #111; height: 400px; display: flex; align-items: center; justify-content: center; border-radius: 10px; border: 1px solid #333; color: #FF4D88;'>
            <h3>⏸ SYSTEM PAUSED<br><span style='font-size: 16px; color: #888;'>Click RESUME to continue monitoring</span></h3>
        </div>
    """, unsafe_allow_html=True)
    # Vẫn hiện log cũ
    render_analytics(chart_placeholder, log_placeholder, status_placeholder)

else:
    # KHI STOPPED (Mới mở hoặc Reset) -> Trạng thái CHỜ
    cam_placeholder.markdown(f"""
        <div style='background-color: #000; height: 400px; display: flex; align-items: center; justify-content: center; border-radius: 10px; border: 1px solid #222; color: #555;'>
            <h3> SYSTEM READY<br><span style='font-size: 16px;'>Click START to begin</span></h3>
        </div>
    """, unsafe_allow_html=True)
    # Vẫn hiện log cũ (nếu chưa reset)
    render_analytics(chart_placeholder, log_placeholder, status_placeholder)