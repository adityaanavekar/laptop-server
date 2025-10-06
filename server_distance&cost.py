import os
import json
import time
from flask import Flask, request
import google.generativeai as genai
from selenium import webdriver
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC

# ------------------- CONFIG -------------------
GEMINI_API_KEY = "AIzaSyDdDWTQYo_oxr7vJz7OpR5BxlpF-rj7sXI"  # Your API key
WEBPAGE_URL = "https://booking-website-v9ke.vercel.app/"

# Flask app
app = Flask(__name__)

# Configure Gemini
genai.configure(api_key=GEMINI_API_KEY)

def get_distance(from_addr, to_addr, model):
    prompt = (
        f"What is the accurate driving distance in kilometers between the following addresses in India? "
        f"From: {from_addr}\nTo: {to_addr}\n"
        "Base your answer on Google Maps data for road distance, not straight-line. "
        "Respond with JSON only: {'distance_km': integer}"
    )
    response = model.generate_content([{"text": prompt}])
    json_str = response.text.strip().replace("```json", "").replace("```", "").strip()
    dist_data = json.loads(json_str)
    return dist_data['distance_km']

@app.route('/upload', methods=['POST'])
def upload_image():
    # -------- Debug incoming request --------
    print("Request headers:", request.headers)
    print("Request files:", request.files)
    print("Request content-type:", request.content_type)

    # -------- Receive image from ESP32-CAM --------
    image_bytes = None
    if 'image' in request.files:
        print("Found 'image' in request.files")
        image_file = request.files['image']
        image_bytes = image_file.read()  # Read in-memory bytes
    else:
        # Fallback: Try reading raw request body
        print("No 'image' field, attempting to read raw body")
        if request.content_type.startswith('image/jpeg'):
            image_bytes = request.get_data()
        else:
            print("Invalid content-type or no image data")
            return "No image file received or invalid content-type", 400

    print(f"Received image, size: {len(image_bytes)} bytes")

    # -------- Send image to Gemini API --------
    try:
        model = genai.GenerativeModel("gemini-2.5-flash")  # Updated model
        prompt = (
            "Extract postal sender and receiver addresses as JSON only. "
            "Use this structure: {"
            "'from': {'first_name': '', 'last_name': '', 'address': '', 'city': '', 'state': '', 'pincode': '', 'mobile': ''}, "
            "'to': {'first_name': '', 'last_name': '', 'address': '', 'city': '', 'state': '', 'pincode': '', 'mobile': ''}"
            
            "}"
        )
        response = model.generate_content([
            {"mime_type": "image/jpeg", "data": image_bytes},
            {"text": prompt}
        ])
    except Exception as e:
        print(f"Gemini API error: {e}")
        return f"Error calling Gemini API: {e}", 500

    # -------- Parse Gemini response --------
    try:
        json_str = response.text.strip().replace("```json", "").replace("```", "").strip()
        data = json.loads(json_str)
        print("Parsed JSON:", json.dumps(data, indent=2))
    except Exception as e:
        print("Gemini raw output:", response.text)
        return f"Error parsing Gemini response: {e}", 500

    # -------- Calculate distance using full addresses --------
    try:
        from_addr = f"{data['from']['address']}, {data['from']['city']}, {data['from']['state']} {data['from']['pincode']}, India"
        to_addr = f"{data['to']['address']}, {data['to']['city']}, {data['to']['state']} {data['to']['pincode']}, India"
        distance_km = get_distance(from_addr, to_addr, model)
        print(f"Calculated distance: {distance_km} km")
    except Exception as e:
        print(f"Distance calculation error: {e}")
        return f"Error calculating distance: {e}", 500

    # -------- Selenium automation --------
    try:
        driver = webdriver.Chrome()  # Assumes ChromeDriver in PATH
        driver.get(WEBPAGE_URL)
        wait = WebDriverWait(driver, 10)

        # Wait for sender-first input to load
        wait.until(EC.presence_of_element_located((By.ID, "sender-first")))

        # -------- Fill Sender Fields --------
        driver.find_element(By.ID, "sender-first").send_keys(data["from"]["first_name"])
        driver.find_element(By.ID, "sender-last").send_keys(data["from"]["last_name"])
        driver.find_element(By.ID, "sender-addr1").send_keys(data["from"]["address"])
        driver.find_element(By.ID, "sender-city").send_keys(data["from"]["city"])
        driver.find_element(By.ID, "sender-state").send_keys(data["from"]["state"])
        driver.find_element(By.ID, "sender-pin").send_keys(data["from"]["pincode"])
        driver.find_element(By.ID, "sender-mobile").send_keys(data["from"]["mobile"])

        # -------- Fill Recipient Fields --------
        driver.find_element(By.ID, "recipient-first").send_keys(data["to"]["first_name"])
        driver.find_element(By.ID, "recipient-last").send_keys(data["to"]["last_name"])
        driver.find_element(By.ID, "recipient-addr1").send_keys(data["to"]["address"])
        driver.find_element(By.ID, "recipient-city").send_keys(data["to"]["city"])
        driver.find_element(By.ID, "recipient-state").send_keys(data["to"]["state"])
        driver.find_element(By.ID, "recipient-pin").send_keys(data["to"]["pincode"])
        driver.find_element(By.ID, "recipient-mobile").send_keys(data["to"]["mobile"])

        # -------- Fill Calculated Distance & Dummy Weight --------
        driver.find_element(By.ID, "distance-input").send_keys(str(distance_km))
        driver.find_element(By.ID, "weight").send_keys("450")

        # -------- Click Calculate --------
        driver.find_element(By.ID, "calculate-btn").click()

        # Wait for results
        time.sleep(5)

        # Keep browser open for debugging
        # driver.quit()

    except Exception as e:
        print(f"Selenium error: {e}")
        return f"Error in Selenium automation: {e}", 500

    return "✅ Form filled and Calculate clicked successfully", 200

# -------- Run Flask server --------
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
